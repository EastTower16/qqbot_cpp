## 1. Project Skeleton

- [x] 1.1 Create the `src/common`, `src/transport`, `src/openapi`, `src/websocket`, and `src/sdk` module layout for the C++ SDK
- [x] 1.2 Add shared configuration, constants, version metadata, and normalized `SDKError` types used by both OpenAPI and WebSocket flows
- [x] 1.3 Add top-level public SDK entrypoints that mirror the Node SDK mental model for creating OpenAPI and WebSocket clients

## 2. OpenAPI Foundation

- [x] 2.1 Implement the version registry and default version selection flow for the C++ OpenAPI client factory
- [x] 2.2 Implement the shared HTTP request executor that injects Authorization, User-Agent, and sandbox-aware base URLs
- [x] 2.3 Add the base OpenAPI client and initial `v1` module wiring so business API modules can reuse the shared executor
- [x] 2.4 Normalize HTTP transport failures into `SDKError` objects that preserve status code, trace ID, and server diagnostics

## 3. WebSocket Foundation

- [x] 3.1 Implement gateway bootstrap logic that fetches gateway metadata and initializes session state from bot configuration
- [x] 3.2 Implement gateway connection handling for HELLO, IDENTIFY/RESUME, HEARTBEAT, DISPATCH, and CLOSE flows
- [x] 3.3 Implement `SessionState` persistence for `session_id`, sequence number, heartbeat state, and resume context
- [x] 3.4 Implement retry and reconnect handling that resumes when allowed and surfaces terminal failure after retry exhaustion

## 4. Validation

- [x] 4.1 Add focused tests or runnable checks for OpenAPI factory creation, shared request header injection, and sandbox URL resolution
- [x] 4.2 Add focused tests or runnable checks for gateway bootstrap, heartbeat scheduling, dispatch delivery, and reconnect behavior
- [x] 4.3 Add a minimal integration example or smoke test showing top-level creation of both OpenAPI and WebSocket clients from the public SDK surface
