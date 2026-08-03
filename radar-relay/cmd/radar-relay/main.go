package main

import (
	"bufio"
	"context"
	"crypto/tls"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	"github.com/tiansongyu/cs2_cheat/radar-relay/internal/auth"
	"github.com/tiansongyu/cs2_cheat/radar-relay/internal/config"
	"github.com/tiansongyu/cs2_cheat/radar-relay/internal/relay"
)

const usage = `Radar Relay

Usage:
  radar-relay serve -config /path/to/config.json
  radar-relay token
  radar-relay hash-token                 # reads the token from stdin
  radar-relay init-config [options]
  radar-relay check-config -config /path/to/config.json [-origin https://radar.example.com]
  radar-relay healthcheck [-url http://127.0.0.1:8080/healthz]
`

func main() {
	if err := run(os.Args[1:], os.Stdin, os.Stdout, os.Stderr); err != nil {
		_, _ = fmt.Fprintln(os.Stderr, "radar-relay:", err)
		os.Exit(1)
	}
}

func run(args []string, stdin io.Reader, stdout, stderr io.Writer) error {
	if len(args) == 0 {
		_, _ = io.WriteString(stderr, usage)
		return errors.New("a command is required")
	}
	switch args[0] {
	case "serve":
		return serve(args[1:], stderr)
	case "token":
		return generateToken(args[1:], stdout)
	case "hash-token":
		return hashToken(args[1:], stdin, stdout)
	case "init-config":
		return initConfig(args[1:], stdout, stderr)
	case "check-config":
		return checkConfig(args[1:], stdout, stderr)
	case "healthcheck":
		return healthcheck(args[1:])
	case "help", "-h", "--help":
		_, _ = io.WriteString(stdout, usage)
		return nil
	default:
		return fmt.Errorf("unknown command %q", args[0])
	}
}

func checkConfig(args []string, stdout, stderr io.Writer) error {
	flags := flag.NewFlagSet("check-config", flag.ContinueOnError)
	flags.SetOutput(stderr)
	configPath := flags.String("config", "radar-relay.json", "path to the JSON configuration")
	expectedOrigin := flags.String("origin", "", "optional expected public origin from the reverse proxy")
	if err := flags.Parse(args); err != nil {
		return err
	}
	if flags.NArg() != 0 {
		return errors.New("check-config does not accept positional arguments")
	}
	cfg, err := config.Load(*configPath)
	if err != nil {
		return fmt.Errorf("load config: %w", err)
	}
	if *expectedOrigin != "" && cfg.PublicOrigin != *expectedOrigin {
		return fmt.Errorf("publicOrigin mismatch: config has %q, expected %q", cfg.PublicOrigin, *expectedOrigin)
	}
	_, err = fmt.Fprintf(stdout, "configuration valid (rooms: %d, publicOrigin: %s)\n", len(cfg.Rooms), cfg.PublicOrigin)
	return err
}

func serve(args []string, stderr io.Writer) error {
	flags := flag.NewFlagSet("serve", flag.ContinueOnError)
	flags.SetOutput(stderr)
	configPath := flags.String("config", "radar-relay.json", "path to the JSON configuration")
	if err := flags.Parse(args); err != nil {
		return err
	}
	if flags.NArg() != 0 {
		return errors.New("serve does not accept positional arguments")
	}
	cfg, err := config.Load(*configPath)
	if err != nil {
		return fmt.Errorf("load config: %w", err)
	}
	logger := slog.New(slog.NewJSONHandler(stderr, &slog.HandlerOptions{Level: slog.LevelInfo}))
	relayServer, err := relay.New(cfg, logger)
	if err != nil {
		return fmt.Errorf("initialize relay: %w", err)
	}
	defer relayServer.Close()

	httpServer := &http.Server{
		Addr:              cfg.Listen,
		Handler:           relayServer.Handler(),
		ReadHeaderTimeout: 5 * time.Second,
		ReadTimeout:       10 * time.Second,
		WriteTimeout:      15 * time.Second,
		IdleTimeout:       75 * time.Second,
		MaxHeaderBytes:    16 * 1024,
		TLSConfig: &tls.Config{
			MinVersion: tls.VersionTLS12,
		},
	}
	serveErrors := make(chan error, 1)
	go func() {
		logger.Info("radar relay listening", "listen", cfg.Listen, "rooms", len(cfg.Rooms), "tls", cfg.TLSCertFile != "", "metrics", cfg.EnableMetrics)
		if cfg.TLSCertFile != "" {
			serveErrors <- httpServer.ListenAndServeTLS(cfg.TLSCertFile, cfg.TLSKeyFile)
			return
		}
		serveErrors <- httpServer.ListenAndServe()
	}()

	signals := make(chan os.Signal, 1)
	signal.Notify(signals, syscall.SIGINT, syscall.SIGTERM)
	defer signal.Stop(signals)
	select {
	case received := <-signals:
		logger.Info("shutdown requested", "signal", received.String())
	case serveErr := <-serveErrors:
		if !errors.Is(serveErr, http.ErrServerClosed) {
			return serveErr
		}
		return nil
	}

	relayServer.SetReady(false)
	shutdownContext, cancel := context.WithTimeout(context.Background(), cfg.ShutdownTimeout())
	defer cancel()
	if err := httpServer.Shutdown(shutdownContext); err != nil {
		_ = httpServer.Close()
		return fmt.Errorf("graceful HTTP shutdown: %w", err)
	}
	relayServer.Close()
	logger.Info("radar relay stopped")
	return nil
}

func generateToken(args []string, stdout io.Writer) error {
	if len(args) != 0 {
		return errors.New("token does not accept arguments")
	}
	token, err := auth.GenerateToken()
	if err != nil {
		return err
	}
	_, err = fmt.Fprintf(stdout, "token: %s\nsha256: %s\n", token, auth.HexSum(token))
	return err
}

func hashToken(args []string, stdin io.Reader, stdout io.Writer) error {
	if len(args) != 0 {
		return errors.New("hash-token does not accept arguments; pass the secret on stdin")
	}
	scanner := bufio.NewScanner(io.LimitReader(stdin, 4096))
	if !scanner.Scan() {
		if err := scanner.Err(); err != nil {
			return err
		}
		return errors.New("stdin did not contain a token")
	}
	token := strings.TrimSuffix(scanner.Text(), "\r")
	if scanner.Scan() {
		return errors.New("stdin must contain exactly one token line")
	}
	if !auth.ValidPresentedToken(token) {
		return errors.New("token must be 24-512 characters without whitespace or control characters")
	}
	_, err := fmt.Fprintln(stdout, auth.HexSum(token))
	return err
}

func initConfig(args []string, stdout, stderr io.Writer) error {
	flags := flag.NewFlagSet("init-config", flag.ContinueOnError)
	flags.SetOutput(stderr)
	output := flags.String("out", "radar-relay.json", "new configuration path (must not exist)")
	origin := flags.String("origin", "", "public HTTPS origin, for example https://radar.example.com")
	roomID := flags.String("room", "main", "initial room id")
	listen := flags.String("listen", "127.0.0.1:8080", "listen address")
	staticDir := flags.String("static-dir", "", "optional Web Radar dist directory")
	if err := flags.Parse(args); err != nil {
		return err
	}
	if flags.NArg() != 0 || *origin == "" {
		return errors.New("init-config requires -origin and accepts no positional arguments")
	}
	producerToken, err := auth.GenerateToken()
	if err != nil {
		return err
	}
	inviteToken, err := auth.GenerateToken()
	if err != nil {
		return err
	}
	cfg := config.Default()
	cfg.Listen = *listen
	cfg.PublicOrigin = *origin
	cfg.StaticDir = *staticDir
	cfg.Rooms = []config.RoomConfig{{
		ID:                  *roomID,
		ProducerTokenSHA256: auth.HexSum(producerToken),
		InviteTokenSHA256:   []string{auth.HexSum(inviteToken)},
		MaxViewers:          32,
	}}
	if err := cfg.Validate(); err != nil {
		return err
	}
	payload, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return err
	}
	payload = append(payload, '\n')
	file, err := os.OpenFile(*output, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0600)
	if err != nil {
		return err
	}
	if _, err = file.Write(payload); err != nil {
		_ = file.Close()
		return err
	}
	if err := file.Close(); err != nil {
		return err
	}
	_, err = fmt.Fprintf(stdout,
		"created %s\nroom: %s\nproducer token: %s\ninvite token: %s\n\nStore these tokens in separate secret managers; the config contains hashes only.\n",
		*output, *roomID, producerToken, inviteToken)
	return err
}

func healthcheck(args []string) error {
	flags := flag.NewFlagSet("healthcheck", flag.ContinueOnError)
	flags.SetOutput(io.Discard)
	endpoint := flags.String("url", "http://127.0.0.1:8080/healthz", "health endpoint")
	if err := flags.Parse(args); err != nil {
		return err
	}
	if flags.NArg() != 0 {
		return errors.New("healthcheck does not accept positional arguments")
	}
	client := &http.Client{Timeout: 2 * time.Second}
	response, err := client.Get(*endpoint)
	if err != nil {
		return err
	}
	defer response.Body.Close()
	_, _ = io.Copy(io.Discard, io.LimitReader(response.Body, 4096))
	if response.StatusCode != http.StatusOK {
		return fmt.Errorf("health endpoint returned %s", response.Status)
	}
	return nil
}
