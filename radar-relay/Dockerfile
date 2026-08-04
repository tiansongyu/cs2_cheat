FROM node:22-alpine@sha256:c610fcdfb1d5b4740dd70c284ed3cb16bb857e0f7166196e36a5501df7a3aa32 AS web

WORKDIR /web-radar
COPY web-radar/package.json web-radar/package-lock.json ./
RUN npm ci
COPY web-radar/index.html web-radar/vite.config.ts web-radar/tsconfig*.json ./
COPY web-radar/src ./src
COPY web-radar/public ./public
COPY web-radar/scripts ./scripts
RUN npm test \
    && npm run build \
    && node scripts/validate-bundle.mjs dist

FROM golang:1.26.5-alpine@sha256:0178a641fbb4858c5f1b48e34bdaabe0350a330a1b1149aabd498d0699ff5fb2 AS build

WORKDIR /src
COPY radar-relay/go.mod radar-relay/go.sum ./
COPY radar-relay/vendor ./vendor
COPY radar-relay/cmd ./cmd
COPY radar-relay/internal ./internal

RUN go test -mod=vendor ./...
RUN CGO_ENABLED=0 GOOS=linux go build -mod=vendor -trimpath \
    -ldflags="-s -w -buildid=" -o /out/radar-relay ./cmd/radar-relay

FROM scratch

COPY --from=build --chown=65532:65532 /out/radar-relay /radar-relay
COPY --from=web --chown=65532:65532 /web-radar/dist /srv/web-radar
COPY --chown=65532:65532 LICENSE /licenses/project-LICENSE
COPY --chown=65532:65532 radar-relay/THIRD_PARTY_NOTICES.md /licenses/THIRD_PARTY_NOTICES.md
COPY --chown=65532:65532 radar-relay/vendor/github.com/gorilla/websocket/LICENSE /licenses/gorilla-websocket-LICENSE

USER 65532:65532
EXPOSE 8080
HEALTHCHECK --interval=15s --timeout=3s --start-period=5s --retries=3 \
  CMD ["/radar-relay", "healthcheck", "-url", "http://127.0.0.1:8080/healthz"]

ENTRYPOINT ["/radar-relay"]
CMD ["serve", "-config", "/run/secrets/relay-config.json"]
