package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"mime/multipart"
	"net"
	"net/http"
	"net/http/httputil"
	"net/url"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

type Config struct {
	URL   string `json:"url"`
	Token string `json:"token"`
}
type Client struct {
	Config
	HTTP *http.Client
}

func newClient(config Config) *Client {
	return &Client{Config: config, HTTP: &http.Client{Timeout: 5 * time.Minute, CheckRedirect: func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse }}}
}
func (c *Client) do(method, path string, body io.Reader, contentType string, length int64) ([]byte, error) {
	req, err := http.NewRequest(method, strings.TrimRight(c.URL, "/")+path, body)
	if err != nil {
		return nil, err
	}
	req.Header.Set("Authorization", "Bearer "+c.Token)
	if contentType != "" {
		req.Header.Set("Content-Type", contentType)
	}
	if length >= 0 {
		req.ContentLength = length
	}
	response, err := c.HTTP.Do(req)
	if err != nil {
		return nil, fmt.Errorf("device request failed: %w", err)
	}
	defer response.Body.Close()
	data, err := io.ReadAll(io.LimitReader(response.Body, 2<<20))
	if err != nil {
		return nil, err
	}
	if response.StatusCode < 200 || response.StatusCode >= 300 {
		var message struct {
			Error string `json:"error"`
		}
		_ = json.Unmarshal(data, &message)
		if message.Error == "" {
			message.Error = response.Status
		}
		return nil, fmt.Errorf("device: %s", message.Error)
	}
	return data, nil
}
func (c *Client) get(path string) ([]byte, error) { return c.do(http.MethodGet, path, nil, "", 0) }
func (c *Client) form(path string, values url.Values) ([]byte, error) {
	body := values.Encode()
	return c.do(http.MethodPost, path, strings.NewReader(body), "application/x-www-form-urlencoded", int64(len(body)))
}
func (c *Client) control(action, value string) ([]byte, error) {
	return c.form("/api/control", url.Values{"action": {action}, "value": {value}})
}
func (c *Client) playlist(args []string) ([]byte, error) {
	if len(args) == 0 {
		return c.get("/api/playlists")
	}
	action := args[0]
	if action == "show" && len(args) == 2 {
		id, err := playlistNumber(args[1], 1, 16)
		if err != nil {
			return nil, err
		}
		var result struct {
			ID        int               `json:"id"`
			Name      string            `json:"name"`
			Count     int               `json:"count"`
			Available int               `json:"available"`
			Tracks    []json.RawMessage `json:"tracks"`
		}
		offset := 0
		for {
			data, err := c.get("/api/playlists?" + url.Values{"id": {id}, "offset": {strconv.Itoa(offset)}}.Encode())
			if err != nil {
				return nil, err
			}
			var page struct {
				ID        int               `json:"id"`
				Name      string            `json:"name"`
				Count     int               `json:"count"`
				Available int               `json:"available"`
				Tracks    []json.RawMessage `json:"tracks"`
				Next      int               `json:"next_offset"`
			}
			if err := json.Unmarshal(data, &page); err != nil {
				return nil, err
			}
			result.ID, result.Name, result.Count, result.Available = page.ID, page.Name, page.Count, page.Available
			result.Tracks = append(result.Tracks, page.Tracks...)
			if page.Next < 0 {
				break
			}
			if page.Next <= offset || page.Next > 128 {
				return nil, errors.New("device returned an invalid playlist cursor")
			}
			offset = page.Next
		}
		return json.Marshal(result)
	}
	if action == "play" && len(args) == 2 {
		id := args[1]
		if id != "all" {
			var err error
			id, err = playlistNumber(id, 1, 16)
			if err != nil {
				return nil, err
			}
		}
		return c.control("playlist", id)
	}
	counts := map[string]int{"create": 2, "rename": 3, "delete": 2, "add": 3, "remove": 3, "move": 4}
	if counts[action] == 0 || len(args) != counts[action] {
		return nil, errors.New("usage: playlist [show ID | create NAME | rename ID NAME | delete ID | add ID TRACK_ID | remove ID POSITION | move ID FROM TO | play ID|all]")
	}
	values := url.Values{"action": {action}}
	if action == "create" {
		values.Set("value", strings.TrimSpace(args[1]))
	} else {
		id, err := playlistNumber(args[1], 1, 16)
		if err != nil {
			return nil, err
		}
		values.Set("id", id)
		if len(args) >= 3 {
			values.Set("value", args[2])
		}
	}
	if action == "create" || action == "rename" {
		name := strings.TrimSpace(values.Get("value"))
		if len(name) == 0 || len(name) > 63 || strings.ContainsAny(name, "\r\n\t\x00") {
			return nil, errors.New("playlist name must be 1-63 bytes without control characters")
		}
		values.Set("value", name)
	}
	if action == "add" || action == "remove" || action == "move" {
		max := 127
		if action == "add" {
			max = 10000
		}
		value, err := playlistNumber(args[2], 0, max)
		if err != nil {
			return nil, err
		}
		values.Set("value", value)
	}
	if action == "move" {
		to, err := playlistNumber(args[3], 0, 127)
		if err != nil {
			return nil, err
		}
		values.Set("to", to)
	}
	return c.form("/api/playlists", values)
}
func playlistNumber(value string, min, max int) (string, error) {
	n, err := strconv.Atoi(value)
	if err != nil || n < min || n > max {
		return "", fmt.Errorf("expected a number from %d to %d", min, max)
	}
	return strconv.Itoa(n), nil
}
func (c *Client) upload(path, destination string, firmware bool) ([]byte, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer file.Close()
	info, err := file.Stat()
	if err != nil {
		return nil, err
	}
	if !info.Mode().IsRegular() || info.Size() == 0 {
		return nil, errors.New("upload requires a nonempty regular file")
	}
	maximum := int64(256 << 20)
	if firmware {
		maximum = 3 << 20
	}
	if info.Size() > maximum {
		return nil, fmt.Errorf("file exceeds %d-byte device limit", maximum)
	}
	// Stream the file while declaring an exact Content-Length. The ESP32's
	// HTTP server does not accept chunked request bodies.
	var framing bytes.Buffer
	writer := multipart.NewWriter(&framing)
	if _, err = writer.CreateFormFile("file", filepath.Base(path)); err != nil {
		return nil, err
	}
	prefixLen := framing.Len()
	if err = writer.Close(); err != nil {
		return nil, err
	}
	all := framing.Bytes()
	body := io.MultiReader(bytes.NewReader(all[:prefixLen]), file, bytes.NewReader(all[prefixLen:]))
	endpoint := "/api/upload"
	params := url.Values{"size": {strconv.FormatInt(info.Size(), 10)}}
	if firmware {
		endpoint = "/api/firmware"
	} else {
		params.Set("path", destination)
	}
	return c.do(http.MethodPost, endpoint+"?"+params.Encode(), body, writer.FormDataContentType(), int64(len(all))+info.Size())
}
func configPath() (string, error) {
	directory, err := os.UserConfigDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(directory, "cardtunes", "config.json"), nil
}
func loadConfig() Config {
	path, err := configPath()
	if err != nil {
		return Config{}
	}
	data, err := os.ReadFile(path)
	if err != nil {
		return Config{}
	}
	var config Config
	_ = json.Unmarshal(data, &config)
	return config
}
func saveConfig(config Config) error {
	path, err := configPath()
	if err != nil {
		return err
	}
	if err = os.MkdirAll(filepath.Dir(path), 0700); err != nil {
		return err
	}
	data, err := json.MarshalIndent(config, "", "  ")
	if err != nil {
		return err
	}
	temp, err := os.CreateTemp(filepath.Dir(path), "config-*.json")
	if err != nil {
		return err
	}
	defer os.Remove(temp.Name())
	if err = temp.Chmod(0600); err == nil {
		_, err = temp.Write(append(data, '\n'))
	}
	closeErr := temp.Close()
	if err != nil {
		return err
	}
	if closeErr != nil {
		return closeErr
	}
	return os.Rename(temp.Name(), path)
}
func normalizeHost(raw string) (string, error) {
	if !strings.Contains(raw, "://") {
		raw = "http://" + raw
	}
	u, err := url.Parse(raw)
	if err != nil || u.Host == "" || (u.Scheme != "http" && u.Scheme != "https") || u.User != nil || u.RawQuery != "" || u.Fragment != "" || (u.Path != "" && u.Path != "/") {
		return "", errors.New("host must be an HTTP(S) device address without a path")
	}
	return strings.TrimRight(u.String(), "/"), nil
}
func output(data []byte) error {
	var pretty bytes.Buffer
	if json.Indent(&pretty, data, "", "  ") == nil {
		_, err := fmt.Println(pretty.String())
		return err
	}
	_, err := os.Stdout.Write(data)
	return err
}

type uploadItem struct{ local, destination string }

func musicFiles(paths []string) ([]uploadItem, error) {
	var files []uploadItem
	for _, root := range paths {
		info, err := os.Stat(root)
		if err != nil {
			return nil, err
		}
		root = filepath.Clean(root)
		err = filepath.WalkDir(root, func(path string, entry os.DirEntry, walkErr error) error {
			if walkErr != nil {
				return walkErr
			}
			if strings.HasPrefix(entry.Name(), ".") {
				if entry.IsDir() {
					return filepath.SkipDir
				}
				return nil
			}
			if entry.IsDir() {
				return nil
			}
			if entry.Type()&os.ModeSymlink != 0 {
				return nil
			}
			if !strings.EqualFold(filepath.Ext(entry.Name()), ".mp3") {
				return nil
			}
			relative := filepath.Base(path)
			if info.IsDir() {
				relative, err = filepath.Rel(filepath.Dir(root), path)
				if err != nil {
					return err
				}
			}
			destination := "/Music/" + filepath.ToSlash(relative)
			if len(destination) >= 192 {
				return fmt.Errorf("device path is too long: %s", destination)
			}
			files = append(files, uploadItem{path, destination})
			return nil
		})
		if err != nil {
			return nil, err
		}
	}
	if len(files) == 0 {
		return nil, errors.New("no MP3 files found")
	}
	return files, nil
}
func usage() {
	fmt.Println(`Cardtunes: Wi-Fi remote for the Cardputer-Adv

cardtunes [--host ADDRESS] [--token KEY] COMMAND

pair ADDRESS KEY            Verify and remember a device (private config)
status                      Playback, battery, Wi-Fi and diagnostics as JSON
list [SEARCH]               Search the complete paginated library
play [ID]                   Play a track or resume
pause | toggle | stop       Playback controls
next | previous             Navigate playback order
seek SECONDS                Absolute position in the current track
volume 0..100               Set volume
shuffle on|off              Shuffle playback order
repeat off|all|one          Set repeat mode
queue [ID]                  Read the queue or add a track
playlist                    List saved playlists
playlist show ID            Read a playlist (including unavailable files)
playlist create NAME        Create an empty named playlist
playlist rename ID NAME     Rename a playlist
playlist add ID TRACK_ID    Add a library track to a playlist
playlist remove ID POSITION Remove a zero-based playlist entry
playlist move ID FROM TO    Reorder zero-based playlist positions
playlist delete ID          Delete the list, keeping music files
playlist play ID|all        Play only this playlist, or all music
play-library ID             Play a library track and leave playlist mode
clear-queue | rescan        Clear upcoming tracks or rescan microSD
upload FILE_OR_FOLDER...    Upload MP3s, retaining album folders
screen FILE.bmp             Capture the actual device display
key KEY                     Send a device key (tab, enter, [, ], n, etc.)
firmware FILE.bin           Install a firmware update over Wi-Fi
serve TAILSCALE_IP:PORT      Proxy the web UI on this machine's Tailscale IP

CARDTUNES_HOST and CARDTUNES_TOKEN override saved connection settings.`)
}
func run(args []string) error {
	config := loadConfig()
	if value := os.Getenv("CARDTUNES_HOST"); value != "" {
		config.URL = value
	}
	if value := os.Getenv("CARDTUNES_TOKEN"); value != "" {
		config.Token = value
	}
	flags := flag.NewFlagSet("cardtunes", flag.ContinueOnError)
	flags.StringVar(&config.URL, "host", config.URL, "Device address")
	flags.StringVar(&config.Token, "token", config.Token, "Device access key")
	if err := flags.Parse(args); err != nil {
		return err
	}
	args = flags.Args()
	if len(args) == 0 || args[0] == "help" {
		usage()
		return nil
	}
	command := args[0]
	args = args[1:]
	if command == "pair" {
		if len(args) != 2 {
			return errors.New("usage: cardtunes pair ADDRESS KEY")
		}
		config = Config{URL: args[0], Token: args[1]}
	}
	var err error
	if config.URL == "" || config.Token == "" {
		return errors.New("no saved device; run cardtunes pair ADDRESS KEY")
	}
	config.URL, err = normalizeHost(config.URL)
	if err != nil {
		return err
	}
	c := newClient(config)
	switch command {
	case "playlist":
		data, err := c.playlist(args)
		if err != nil {
			return err
		}
		return output(data)
	case "pair":
		if _, err = c.get("/api/status"); err != nil {
			return err
		}
		if err = saveConfig(config); err != nil {
			return err
		}
		fmt.Println("Paired with " + config.URL)
		return nil
	case "status":
		data, err := c.get("/api/status")
		if err != nil {
			return err
		}
		return output(data)
	case "list":
		offset := 0
		tracks := []json.RawMessage{}
		for {
			data, err := c.get("/api/library?" + url.Values{"offset": {strconv.Itoa(offset)}, "q": {strings.Join(args, " ")}}.Encode())
			if err != nil {
				return err
			}
			var page struct {
				Tracks []json.RawMessage `json:"tracks"`
				Next   int               `json:"next_offset"`
			}
			if err = json.Unmarshal(data, &page); err != nil {
				return err
			}
			tracks = append(tracks, page.Tracks...)
			if page.Next < 0 {
				break
			}
			if page.Next <= offset {
				return errors.New("device returned a non-advancing library cursor")
			}
			offset = page.Next
		}
		data, err := json.Marshal(tracks)
		if err != nil {
			return err
		}
		return output(data)
	case "upload":
		if len(args) == 0 {
			return errors.New("usage: cardtunes upload FILE_OR_FOLDER...")
		}
		files, err := musicFiles(args)
		if err != nil {
			return err
		}
		for i, file := range files {
			fmt.Fprintf(os.Stderr, "[%d/%d] %s\n", i+1, len(files), file.destination)
			data, err := c.upload(file.local, file.destination, false)
			if err != nil {
				return err
			}
			if err = output(data); err != nil {
				return err
			}
		}
		return nil
	case "firmware":
		if len(args) != 1 {
			return errors.New("usage: cardtunes firmware FILE.bin")
		}
		data, err := c.upload(args[0], "", true)
		if err != nil {
			return err
		}
		return output(data)
	case "screen":
		if len(args) != 1 {
			return errors.New("usage: cardtunes screen FILE.bmp")
		}
		data, err := c.get("/api/screen.bmp")
		if err != nil {
			return err
		}
		if len(data) < 54 || string(data[:2]) != "BM" {
			return errors.New("invalid device screenshot")
		}
		if err = os.WriteFile(args[0], data, 0600); err != nil {
			return err
		}
		fmt.Println(args[0])
		return nil
	case "key":
		if len(args) != 1 {
			return errors.New("usage: cardtunes key KEY")
		}
		data, err := c.form("/api/input", url.Values{"key": {args[0]}})
		if err != nil {
			return err
		}
		return output(data)
	case "serve":
		if len(args) != 1 {
			return errors.New("usage: cardtunes serve TAILSCALE_IP:PORT")
		}
		host, _, err := net.SplitHostPort(args[0])
		if err != nil {
			return err
		}
		ip := net.ParseIP(host)
		if ip == nil || !inTailscaleRange(ip) {
			return errors.New("serve must bind to this machine's Tailscale IPv4 address")
		}
		target, _ := url.Parse(config.URL)
		proxy := httputil.NewSingleHostReverseProxy(target)
		director := proxy.Director
		proxy.Director = func(req *http.Request) {
			director(req)
			req.Host = target.Host
			if strings.HasPrefix(req.URL.Path, "/api/") {
				req.Header.Set("Authorization", "Bearer "+config.Token)
			}
		}
		proxy.ModifyResponse = func(response *http.Response) error {
			if response.Request.URL.Path == "/" {
				data, err := io.ReadAll(response.Body)
				response.Body.Close()
				if err != nil {
					return err
				}
				// The tailnet proxy owns the credential. A harmless marker
				// starts the UI; the real device key never enters the browser.
				data = bytes.Replace(data, []byte("let key = localStorage.getItem('cardtunes.key') || ''"), []byte("let key = 'tailnet-proxy'"), 1)
				response.Body = io.NopCloser(bytes.NewReader(data))
				response.ContentLength = int64(len(data))
				response.Header.Set("Content-Length", strconv.Itoa(len(data)))
			}
			return nil
		}
		fmt.Println("Cardtunes proxy: http://" + args[0])
		server := &http.Server{Addr: args[0], Handler: protectProxy(proxy, args[0]), ReadHeaderTimeout: 10 * time.Second, IdleTimeout: 60 * time.Second}
		return server.ListenAndServe()
	}
	value := ""
	if len(args) > 1 {
		return errors.New("too many command arguments")
	}
	if len(args) == 1 {
		value = args[0]
	}
	if command == "queue" {
		if value == "" {
			data, err := c.get("/api/queue")
			if err != nil {
				return err
			}
			return output(data)
		}
		command = "enqueue"
	}
	switch command {
	case "play", "play-library", "pause", "toggle", "stop", "next", "previous", "seek", "volume", "shuffle", "repeat", "enqueue", "clear-queue", "rescan":
		data, err := c.control(command, value)
		if err != nil {
			return err
		}
		return output(data)
	default:
		return fmt.Errorf("unknown command %q", command)
	}
}
func inTailscaleRange(ip net.IP) bool {
	_, network, _ := net.ParseCIDR("100.64.0.0/10")
	return network.Contains(ip)
}
func protectProxy(next http.Handler, address string) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		// Prevent DNS rebinding and cross-site forms from borrowing the proxy's
		// device credential. The UI's custom header requires a CORS preflight.
		origin := r.Header.Get("Origin")
		if r.Host != address || (origin != "" && origin != "http://"+address) ||
			(strings.HasPrefix(r.URL.Path, "/api/") && r.Header.Get("Authorization") != "Bearer tailnet-proxy") {
			http.Error(w, "Same-origin browser access required", http.StatusForbidden)
			return
		}
		next.ServeHTTP(w, r)
	})
}
func main() {
	if err := run(os.Args[1:]); err != nil {
		fmt.Fprintln(os.Stderr, "cardtunes:", err)
		os.Exit(1)
	}
}
