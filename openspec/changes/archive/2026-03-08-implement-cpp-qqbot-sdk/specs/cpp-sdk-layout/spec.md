## ADDED Requirements

### Requirement: C++ SDK SHALL organize code under a stable src module layout
The SDK SHALL place implementation code under `src/` with stable module boundaries for common types, transport abstractions, OpenAPI modules, WebSocket modules, and public facade entrypoints.

#### Scenario: Locate public SDK entrypoint
- **WHEN** a maintainer inspects the `src/` directory
- **THEN** they MUST be able to identify the public entrypoint and the core module groups without tracing unrelated files

#### Scenario: Separate transport and business API responsibilities
- **WHEN** a maintainer adds a new business API module
- **THEN** they MUST be able to depend on shared transport abstractions instead of duplicating low-level networking code

### Requirement: C++ SDK SHALL expose shared configuration and error types across modules
The SDK SHALL define shared configuration, constants, and normalized error types that can be used consistently by both OpenAPI and WebSocket modules.

#### Scenario: Reuse configuration in HTTP and WebSocket flows
- **WHEN** the caller enables sandbox mode and sets retry-related options
- **THEN** both OpenAPI and WebSocket components MUST read from the same configuration model

#### Scenario: Reuse normalized error model
- **WHEN** either HTTP or WebSocket code reports a failure
- **THEN** the caller MUST be able to inspect a shared SDK error structure instead of transport-specific exception types

### Requirement: C++ SDK SHALL provide a migration-friendly public surface
The SDK SHALL expose top-level creation APIs that mirror the conceptual entrypoints of the Node SDK so that maintainers can map reference behavior from `bot-node-sdk` into the C++ implementation.

#### Scenario: Create OpenAPI and WebSocket clients from top-level SDK API
- **WHEN** a caller uses the SDK's top-level public interface
- **THEN** they MUST be able to create both OpenAPI and WebSocket clients without constructing internal modules directly

#### Scenario: Extend API versions without changing caller mental model
- **WHEN** a new OpenAPI version is added later
- **THEN** the SDK MUST support that addition through version registration rather than a breaking reorganization of the public facade
