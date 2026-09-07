package main

import (
	"bytes"
	"encoding/json"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
)

func TestPlaylistCommands(t *testing.T) {
	for _, tc := range []struct {
		args                                []string
		path, method, action, id, value, to string
	}{
		{[]string{}, "/api/playlists", "GET", "", "", "", ""},
		{[]string{"create", "Road & Radio"}, "/api/playlists", "POST", "create", "", "Road & Radio", ""},
		{[]string{"rename", "2", "Evening \"Mix\""}, "/api/playlists", "POST", "rename", "2", "Evening \"Mix\"", ""},
		{[]string{"add", "2", "42"}, "/api/playlists", "POST", "add", "2", "42", ""},
		{[]string{"remove", "2", "0"}, "/api/playlists", "POST", "remove", "2", "0", ""},
		{[]string{"move", "2", "3", "0"}, "/api/playlists", "POST", "move", "2", "3", "0"},
		{[]string{"move", "2", "999", "500"}, "/api/playlists", "POST", "move", "2", "999", "500"},
		{[]string{"remove", "2", "999"}, "/api/playlists", "POST", "remove", "2", "999", ""},
		{[]string{"delete", "2"}, "/api/playlists", "POST", "delete", "2", "", ""},
		{[]string{"play", "2"}, "/api/control", "POST", "playlist", "", "2", ""},
		{[]string{"play", "all"}, "/api/control", "POST", "playlist", "", "all", ""},
	} {
		t.Run(strings.Join(tc.args, " "), func(t *testing.T) {
			server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				if r.Header.Get("Authorization") != "Bearer test-key" || r.URL.Path != tc.path || r.Method != tc.method {
					t.Errorf("incorrect playlist request: %s %s", r.Method, r.URL)
				}
				if err := r.ParseForm(); err != nil {
					t.Error(err)
				}
				for field, want := range map[string]string{"action": tc.action, "id": tc.id, "value": tc.value, "to": tc.to} {
					if r.Form.Get(field) != want {
						t.Errorf("%s: got %q, want %q", field, r.Form.Get(field), want)
					}
				}
				_, _ = w.Write([]byte(`{"ok":true}`))
			}))
			defer server.Close()
			if _, err := newClient(Config{URL: server.URL, Token: "test-key"}).playlist(tc.args); err != nil {
				t.Fatal(err)
			}
		})
	}
}

func TestGenericControlCommands(t *testing.T) {
	for _, tc := range []struct{ action, value string }{{"game", "blocks"}, {"game", "breakout"}, {"game", "2048"}, {"game-key", "primary"}, {"game-key", "pause"}, {"game-exit", ""}, {"play-entry", "0"}, {"play-entry", "999"}, {"queue", "37"}} {
		t.Run(tc.action+" "+tc.value, func(t *testing.T) {
			server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				if r.Method != "POST" || r.URL.Path != "/api/control" || r.Header.Get("Authorization") != "Bearer test-key" {
					t.Error("wrong control request")
				}
				if err := r.ParseForm(); err != nil {
					t.Fatal(err)
				}
				action := tc.action
				if action == "queue" {
					action = "enqueue"
				}
				if r.Form.Get("action") != action || r.Form.Get("value") != tc.value {
					t.Error("wrong control command")
				}
				w.WriteHeader(202)
				_, _ = w.Write([]byte(`{"ok":true}`))
			}))
			defer server.Close()
			t.Setenv("CARDTUNES_HOST", server.URL)
			t.Setenv("CARDTUNES_TOKEN", "test-key")
			args := []string{tc.action}
			if tc.value != "" {
				args = append(args, tc.value)
			}
			if err := run(args); err != nil {
				t.Fatal(err)
			}
		})
	}
}

func runOutput(t *testing.T, args ...string) ([]byte, error) {
	t.Helper()
	file, err := os.CreateTemp(t.TempDir(), "stdout")
	if err != nil {
		t.Fatal(err)
	}
	defer file.Close()
	previous := os.Stdout
	os.Stdout = file
	defer func() { os.Stdout = previous }()
	runErr := run(args)
	if _, err := file.Seek(0, io.SeekStart); err != nil {
		t.Fatal(err)
	}
	data, err := io.ReadAll(file)
	if err != nil {
		t.Fatal(err)
	}
	return data, runErr
}

func TestPlayEntryHelp(t *testing.T) {
	data, err := runOutput(t, "help")
	if err != nil || !strings.Contains(string(data), "play-entry POSITION") || !strings.Contains(string(data), "zero-based entry in the active playlist") {
		t.Fatalf("missing play-entry help: %s (%v)", data, err)
	}
}

func TestQueuePagination(t *testing.T) {
	for _, tc := range []struct{ total, pageSize int }{{0, 8}, {1, 8}, {8, 8}, {9, 8}, {64, 8}, {16, 16}, {17, 16}, {64, 16}} {
		t.Run(strconv.Itoa(tc.total)+"/page"+strconv.Itoa(tc.pageSize), func(t *testing.T) {
			total, pageSize := tc.total, tc.pageSize
			calls := 0
			server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				offset, err := strconv.Atoi(r.URL.Query().Get("offset"))
				if err != nil || offset != calls*pageSize || r.URL.Path != "/api/queue" || r.Method != "GET" || r.Header.Get("Authorization") != "Bearer test-key" {
					t.Errorf("incorrect queue request: %s %s", r.Method, r.URL)
				}
				calls++
				tracks := []map[string]any{}
				end := min(offset+pageSize, total)
				for i := offset; i < end; i++ {
					tracks = append(tracks, map[string]any{"id": i % 3, "title": "Track " + strconv.Itoa(i%3)})
				}
				next := -1
				if end < total {
					next = end
				}
				_ = json.NewEncoder(w).Encode(map[string]any{"tracks": tracks, "total": total, "next_offset": next})
			}))
			defer server.Close()
			t.Setenv("CARDTUNES_HOST", server.URL)
			t.Setenv("CARDTUNES_TOKEN", "test-key")
			data, err := runOutput(t, "queue")
			server.Close()
			if err != nil {
				t.Fatal(err)
			}
			var result struct {
				Tracks []struct {
					ID    int
					Title string
				}
				Total int
				Next  int `json:"next_offset"`
			}
			if err := json.Unmarshal(data, &result); err != nil {
				t.Fatal(err)
			}
			if result.Tracks == nil || len(result.Tracks) != total || result.Total != total || result.Next != -1 || calls != max(1, (total+pageSize-1)/pageSize) {
				t.Fatalf("incomplete queue (%d requests): %s", calls, data)
			}
			for i, track := range result.Tracks {
				if track.ID != i%3 || track.Title != "Track "+strconv.Itoa(i%3) {
					t.Fatalf("lost queue order or duplicate at %d: %+v", i, track)
				}
			}
		})
	}
}

func TestLegacyQueue(t *testing.T) {
	for _, cursor := range []string{"", `,"next_offset":null`} {
		calls := 0
		server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			calls++
			_, _ = w.Write([]byte(`{"tracks":[` + strings.Repeat(`{"id":2},`, 63) + `{"id":2}]` + cursor + `}`))
		}))
		data, err := newClient(Config{URL: server.URL}).queue()
		server.Close()
		if err != nil || calls != 1 || bytes.Count(data, []byte(`"id":2`)) != 64 {
			t.Fatalf("legacy queue: %s, calls=%d, error=%v", data, calls, err)
		}
	}
}

func TestQueueRejectsInvalidPages(t *testing.T) {
	for _, tc := range []struct {
		name, response string
		status         int
	}{
		{"non-advancing", `{"tracks":[],"next_offset":16}`, 200},
		{"negative", `{"tracks":[],"next_offset":-2}`, 200},
		{"out of range", `{"tracks":[],"next_offset":64}`, 200},
		{"fractional", `{"tracks":[],"next_offset":16.5}`, 200},
		{"string", `{"tracks":[],"next_offset":"32"}`, 200},
		{"missing tracks", `{}`, 200},
		{"too many tracks", `{"tracks":[` + strings.Repeat(`{"id":0},`, 64) + `{"id":0}],"next_offset":-1}`, 200},
		{"malformed JSON", `{`, 200},
		{"request failure", `{"error":"Queue unavailable"}`, 503},
	} {
		t.Run(tc.name, func(t *testing.T) {
			calls := 0
			server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				calls++
				if calls == 1 {
					_, _ = w.Write([]byte(`{"tracks":[{"id":3}],"next_offset":16}`))
					return
				}
				w.WriteHeader(tc.status)
				_, _ = w.Write([]byte(tc.response))
			}))
			defer server.Close()
			t.Setenv("CARDTUNES_HOST", server.URL)
			t.Setenv("CARDTUNES_TOKEN", "test-key")
			data, err := runOutput(t, "queue")
			server.Close()
			if err == nil || len(data) != 0 || calls != 2 {
				t.Fatalf("invalid queue emitted partial output or retried: %s, calls=%d, error=%v", data, calls, err)
			}
		})
	}
}

func TestLibrarySixteenTrackPages(t *testing.T) {
	calls := 0
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		offset, err := strconv.Atoi(r.URL.Query().Get("offset"))
		if err != nil || offset != calls*48 || r.URL.Path != "/api/library" || r.URL.Query().Get("q") != "Mix & Match" {
			t.Errorf("incorrect library cursor or search: %s", r.URL)
		}
		calls++
		tracks := []map[string]int{}
		end := min(offset+48, 99)
		for id := offset; id < end; id += 3 {
			tracks = append(tracks, map[string]int{"id": id})
		}
		next := -1
		if end < 99 {
			next = end
		}
		_ = json.NewEncoder(w).Encode(map[string]any{"tracks": tracks, "total": 99, "next_offset": next})
	}))
	defer server.Close()
	t.Setenv("CARDTUNES_HOST", server.URL)
	t.Setenv("CARDTUNES_TOKEN", "test-key")
	data, err := runOutput(t, "list", "Mix", "&", "Match")
	server.Close()
	var tracks []struct{ ID int }
	if err != nil || json.Unmarshal(data, &tracks) != nil || len(tracks) != 33 || calls != 3 {
		t.Fatalf("incomplete library: %s, calls=%d, error=%v", data, calls, err)
	}
	for i, track := range tracks {
		if track.ID != i*3 {
			t.Fatalf("wrong library entry at %d: %+v", i, track)
		}
	}
}

func TestPlaylistPaginationAndMissingFiles(t *testing.T) {
	requests := 0
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		requests++
		if r.URL.Query().Get("id") != "2" {
			t.Error("wrong playlist")
		}
		if r.URL.Query().Get("offset") == "0" {
			_, _ = w.Write([]byte(`{"id":2,"name":"Road trip","count":2,"available":1,"tracks":[{"id":42,"position":0}],"next_offset":1}`))
		} else if r.URL.Query().Get("offset") == "1" {
			_, _ = w.Write([]byte(`{"id":2,"name":"Road trip","count":2,"available":1,"tracks":[{"id":null,"position":1,"missing":true}],"next_offset":-1}`))
		} else {
			t.Error("unexpected cursor")
			w.WriteHeader(400)
		}
	}))
	defer server.Close()
	data, err := newClient(Config{URL: server.URL, Token: "test-key"}).playlist([]string{"show", "2"})
	if err != nil {
		t.Fatal(err)
	}
	var result struct {
		Tracks           []json.RawMessage
		Count, Available int
	}
	if err = json.Unmarshal(data, &result); err != nil {
		t.Fatal(err)
	}
	if len(result.Tracks) != 2 || result.Count != 2 || result.Available != 1 || requests != 2 {
		t.Fatalf("bad playlist aggregation: %s", data)
	}
}

func TestPlaylistValidationDoesNotContactDevice(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		t.Error("invalid command contacted device")
		w.WriteHeader(500)
	}))
	defer server.Close()
	client := newClient(Config{URL: server.URL, Token: "test-key"})
	for _, args := range [][]string{{"create"}, {"create", ""}, {"create", strings.Repeat("x", 64)}, {"create", "bad\nname"}, {"rename", "2"}, {"rename", "0", "Name"}, {"delete", "17"}, {"add", "1", "-1"}, {"move", "1", "0"}, {"move", "1", "0", "1000"}, {"remove", "1", "1000"}, {"show", "all"}, {"play", "0"}, {"play", "missing"}, {"unknown"}} {
		if _, err := client.playlist(args); err == nil {
			t.Errorf("accepted %v", args)
		}
	}
}

func TestThousandTrackPlaylistPagination(t *testing.T) {
	requests := 0
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		requests++
		offset, _ := strconv.Atoi(r.URL.Query().Get("offset"))
		end := min(offset+32, 1000)
		tracks := []map[string]int{}
		for i := offset; i < end; i++ {
			tracks = append(tracks, map[string]int{"id": i, "position": i})
		}
		next := end
		if end == 1000 {
			next = -1
		}
		_ = json.NewEncoder(w).Encode(map[string]any{"id": 1, "count": 1000, "available": 1000, "tracks": tracks, "next_offset": next})
	}))
	defer server.Close()
	data, err := newClient(Config{URL: server.URL}).playlist([]string{"show", "1"})
	if err != nil {
		t.Fatal(err)
	}
	var result struct{ Tracks []map[string]int }
	if err = json.Unmarshal(data, &result); err != nil {
		t.Fatal(err)
	}
	if len(result.Tracks) != 1000 || result.Tracks[999]["position"] != 999 || requests != 32 {
		t.Fatalf("truncated large playlist: %d tracks, %d requests", len(result.Tracks), requests)
	}
}

func TestPlaylistRejectsNonAdvancingCursor(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write([]byte(`{"id":1,"tracks":[],"next_offset":0}`))
	}))
	defer server.Close()
	if _, err := newClient(Config{URL: server.URL, Token: "test-key"}).playlist([]string{"show", "1"}); err == nil {
		t.Fatal("accepted non-advancing cursor")
	}
}

func TestControlAuthenticationAndEncoding(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "POST" || r.URL.Path != "/api/control" || r.Header.Get("Authorization") != "Bearer test-key" {
			t.Errorf("unexpected authenticated control request")
		}
		if err := r.ParseForm(); err != nil {
			t.Error(err)
		}
		if r.Form.Get("action") != "seek" || r.Form.Get("value") != "10 & 20" {
			t.Errorf("incorrect form: %v", r.Form)
		}
		_, _ = w.Write([]byte(`{"ok":true}`))
	}))
	defer server.Close()
	client := newClient(Config{URL: server.URL, Token: "test-key"})
	if _, err := client.control("seek", "10 & 20"); err != nil {
		t.Fatal(err)
	}
}

func TestMultipartUploadHasExactLength(t *testing.T) {
	content := bytes.Repeat([]byte{0, 255, 3, 8}, 10000)
	path := filepath.Join(t.TempDir(), "Track 1.mp3")
	if err := os.WriteFile(path, content, 0600); err != nil {
		t.Fatal(err)
	}
	for _, firmware := range []bool{false, true} {
		t.Run(strconv.FormatBool(firmware), func(t *testing.T) {
			server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				if r.ContentLength <= int64(len(content)) || len(r.TransferEncoding) != 0 {
					t.Error("upload must not be chunked")
				}
				if r.URL.Query().Get("size") != strconv.Itoa(len(content)) {
					t.Error("incorrect declared file size")
				}
				expect := "/api/upload"
				if firmware {
					expect = "/api/firmware"
				} else if r.URL.Query().Get("path") != "/Music/Album/Track 1.mp3" {
					t.Error("incorrect music destination")
				}
				if r.URL.Path != expect {
					t.Error("incorrect upload endpoint")
				}
				if r.Header.Get("Authorization") != "Bearer test-key" {
					t.Error("upload missing authentication")
				}
				reader, err := r.MultipartReader()
				if err != nil {
					t.Fatal(err)
				}
				part, err := reader.NextPart()
				if err != nil {
					t.Fatal(err)
				}
				if part.FormName() != "file" || part.FileName() != "Track 1.mp3" {
					t.Error("incorrect multipart metadata")
				}
				data, err := io.ReadAll(part)
				if err != nil {
					t.Fatal(err)
				}
				if !bytes.Equal(data, content) {
					t.Error("upload payload was corrupted")
				}
				if _, err := reader.NextPart(); err != io.EOF {
					t.Error("unexpected additional part")
				}
				_, _ = w.Write([]byte(`{"ok":true}`))
			}))
			defer server.Close()
			client := newClient(Config{URL: server.URL, Token: "test-key"})
			if _, err := client.upload(path, "/Music/Album/Track 1.mp3", firmware); err != nil {
				t.Fatal(err)
			}
		})
	}
}

func TestPlaylistImport(t *testing.T) {
	content := []byte("{\"version\":3,\"name\":\"Ordered\",\"count\":2}\n\"/Music/B.mp3\"\n\"/Music/B.mp3\"\n")
	path := filepath.Join(t.TempDir(), "ordered.jsonl")
	if err := os.WriteFile(path, content, 0600); err != nil {
		t.Fatal(err)
	}
	for _, id := range []string{"", "4"} {
		server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			if r.Method != "POST" || r.URL.Path != "/api/playlist-import" || r.Header.Get("Authorization") != "Bearer test-key" || r.URL.Query().Get("id") != id || r.URL.Query().Get("size") != strconv.Itoa(len(content)) {
				t.Error("invalid playlist import request")
			}
			body, err := io.ReadAll(r.Body)
			if err != nil {
				t.Fatal(err)
			}
			if int64(len(body)) != r.ContentLength || len(r.TransferEncoding) != 0 {
				t.Error("wrong import framing")
			}
			r.Body = io.NopCloser(bytes.NewReader(body))
			file, _, err := r.FormFile("file")
			if err != nil {
				t.Fatal(err)
			}
			defer file.Close()
			got, err := io.ReadAll(file)
			if err != nil || !bytes.Equal(got, content) {
				t.Error("import changed playlist order")
			}
			w.WriteHeader(201)
			_, _ = w.Write([]byte(`{"ok":true,"id":4}`))
		}))
		args := []string{"import", path}
		if id != "" {
			args = append(args, id)
		}
		_, err := newClient(Config{URL: server.URL, Token: "test-key"}).playlist(args)
		server.Close()
		if err != nil {
			t.Fatal(err)
		}
	}
	client := newClient(Config{URL: "http://unreachable.invalid"})
	for _, args := range [][]string{{"import"}, {"import", path, "0"}, {"import", path, "17"}, {"import", path, "4", "extra"}} {
		if _, err := client.playlist(args); err == nil {
			t.Fatalf("accepted %v", args)
		}
	}
	if err := os.WriteFile(path, bytes.Repeat([]byte("x"), 400001), 0600); err != nil {
		t.Fatal(err)
	}
	if _, err := client.playlist([]string{"import", path}); err == nil || !strings.Contains(err.Error(), "device limit") {
		t.Fatal("oversized import was not rejected locally")
	}
}

func TestErrorsAndRedirects(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/redirect" {
			http.Redirect(w, r, "/unexpected", 302)
			return
		}
		if r.URL.Path == "/unexpected" {
			t.Error("followed redirect with device credentials")
		}
		w.WriteHeader(401)
		_, _ = w.Write([]byte(`{"error":"Access key required"}`))
	}))
	defer server.Close()
	client := newClient(Config{URL: server.URL, Token: "test-key"})
	if _, err := client.get("/api/status"); err == nil || !strings.Contains(err.Error(), "Access key required") {
		t.Fatalf("missing API error: %v", err)
	}
	if _, err := client.get("/redirect"); err == nil {
		t.Fatal("redirect should be rejected")
	}
}

func TestMusicDiscovery(t *testing.T) {
	root := filepath.Join(t.TempDir(), "Album")
	if err := os.MkdirAll(filepath.Join(root, ".hidden"), 0700); err != nil {
		t.Fatal(err)
	}
	for _, path := range []string{"Track.MP3", "notes.txt", ".metadata.mp3", ".hidden/secret.mp3"} {
		if err := os.WriteFile(filepath.Join(root, path), []byte("fixture"), 0600); err != nil {
			t.Fatal(err)
		}
	}
	if err := os.Symlink(filepath.Join(root, "Track.MP3"), filepath.Join(root, "link.mp3")); err != nil {
		t.Fatal(err)
	}
	files, err := musicFiles([]string{root})
	if err != nil || len(files) != 1 {
		t.Fatalf("unexpected discovered files: %v %v", files, err)
	}
	if files[0].destination != "/Music/Album/Track.MP3" {
		t.Fatal(files[0].destination)
	}
	if _, err := musicFiles([]string{filepath.Join(root, "notes.txt")}); err == nil {
		t.Fatal("non-MP3 must be rejected")
	}
}

func TestHostAndTailnetValidation(t *testing.T) {
	if got, err := normalizeHost("cardtunes.local/"); err != nil || got != "http://cardtunes.local" {
		t.Fatalf("%s %v", got, err)
	}
	for _, host := range []string{"http://user:pass@example.com", "ftp://example.com", "http://example.com/path", "http://example.com?key=secret"} {
		if _, err := normalizeHost(host); err == nil {
			t.Errorf("accepted %s", host)
		}
	}
	for _, address := range []string{"0.0.0.0", "127.0.0.1", "10.0.0.1", "100.128.0.1"} {
		if inTailscaleRange(net.ParseIP(address)) {
			t.Errorf("accepted non-tailnet bind %s", address)
		}
	}
	if !inTailscaleRange(net.ParseIP("100.78.179.94")) {
		t.Error("rejected tailnet address")
	}
}

func TestProxyRejectsCrossSiteCredentialUse(t *testing.T) {
	address := "100.78.179.94:8174"
	handler := protectProxy(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { w.WriteHeader(204) }), address)
	for _, tc := range []struct {
		name, host, origin, auth string
		status                   int
	}{
		{"browser", address, "http://" + address, "Bearer tailnet-proxy", 204},
		{"same-origin fetch", address, "", "Bearer tailnet-proxy", 204},
		{"cross-site form", address, "https://untrusted.example", "", 403},
		{"cross-site fetch", address, "https://untrusted.example", "Bearer tailnet-proxy", 403},
		{"missing header", address, "", "", 403},
		{"rebound host", "untrusted.example", "", "Bearer tailnet-proxy", 403},
	} {
		t.Run(tc.name, func(t *testing.T) {
			r := httptest.NewRequest("POST", "http://"+tc.host+"/api/control", nil)
			r.Header.Set("Origin", tc.origin)
			r.Header.Set("Authorization", tc.auth)
			w := httptest.NewRecorder()
			handler.ServeHTTP(w, r)
			if w.Code != tc.status {
				t.Fatalf("got %d, want %d", w.Code, tc.status)
			}
		})
	}
}
