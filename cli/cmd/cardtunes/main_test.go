package main

import (
	"bytes"
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
