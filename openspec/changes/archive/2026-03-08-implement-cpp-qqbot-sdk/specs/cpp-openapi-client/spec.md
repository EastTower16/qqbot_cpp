## ADDED Requirements

### Requirement: C++ SDK SHALL expose a versioned OpenAPI client factory
The SDK SHALL provide a public factory entrypoint that accepts bot configuration and returns an OpenAPI client bound to a selected API version. If the caller does not explicitly select a version, the SDK MUST use a registered default version.

#### Scenario: Create default OpenAPI client
- **WHEN** the caller creates an OpenAPI client with valid `app_id` and `token`
- **THEN** the SDK returns a client instance bound to the default registered API version

#### Scenario: Reject unknown API version
- **WHEN** the caller selects an API version that has not been registered
- **THEN** the SDK MUST report an error instead of returning a partially initialized client

### Requirement: C++ SDK SHALL apply shared request configuration automatically
The OpenAPI client SHALL apply shared request behavior for every outbound HTTP request, including Authorization header injection, User-Agent injection, and sandbox-aware base URL resolution.

#### Scenario: Apply bot authorization header
- **WHEN** the caller invokes any OpenAPI method with valid bot credentials
- **THEN** the HTTP request MUST contain `Authorization: Bot <app_id>.<token>`

#### Scenario: Resolve sandbox base URL
- **WHEN** the caller enables sandbox mode in configuration
- **THEN** the request URL MUST be built from the sandbox API domain instead of the production API domain

### Requirement: C++ SDK SHALL provide a reusable request execution abstraction
The OpenAPI client SHALL route business API requests through a reusable request execution abstraction so that API modules do not implement their own credential, URL, or low-level transport logic.

#### Scenario: API module uses shared request executor
- **WHEN** a business API module submits a request definition
- **THEN** the shared executor MUST construct and send the final HTTP request on behalf of the module

#### Scenario: Transport failure is normalized
- **WHEN** the underlying HTTP transport returns an error response or throws an exception
- **THEN** the SDK MUST convert it into a normalized SDK error that preserves status and server diagnostic information
