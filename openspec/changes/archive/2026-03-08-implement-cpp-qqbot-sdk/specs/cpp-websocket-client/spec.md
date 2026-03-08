## ADDED Requirements

### Requirement: C++ SDK SHALL establish a gateway session from bot credentials
The SDK SHALL provide a WebSocket client entrypoint that uses bot credentials and configuration to obtain gateway connection metadata, open a gateway connection, and initialize session state.

#### Scenario: Create gateway session successfully
- **WHEN** the caller creates a WebSocket client with valid configuration
- **THEN** the SDK MUST fetch gateway metadata and start a gateway connection attempt

#### Scenario: Report gateway bootstrap failure
- **WHEN** the SDK cannot fetch gateway metadata
- **THEN** the SDK MUST emit or return a connection error that indicates session startup failed

### Requirement: C++ SDK SHALL maintain gateway heartbeat and session resume state
The SDK SHALL process gateway hello events, start heartbeats at the server-provided interval, track the latest sequence number, and preserve `session_id` plus sequence state for resume-capable reconnects.

#### Scenario: Start heartbeat after hello
- **WHEN** the gateway sends a hello payload with a heartbeat interval
- **THEN** the SDK MUST schedule heartbeat frames using that interval

#### Scenario: Update resume state from dispatch events
- **WHEN** the gateway delivers dispatch events with a sequence number and ready payload
- **THEN** the SDK MUST persist the latest sequence number and session identifier for later resume attempts

### Requirement: C++ SDK SHALL support reconnect-aware event delivery
The WebSocket client SHALL deliver ready, dispatch, reconnect, disconnect, and error signals to the caller, and it MUST distinguish resume-capable reconnects from terminal failures.

#### Scenario: Notify dispatch event to caller
- **WHEN** the gateway sends a dispatch event payload
- **THEN** the SDK MUST forward the event type and payload to the caller's event handler

#### Scenario: Attempt resume on resumable disconnect
- **WHEN** the connection closes with a gateway close reason that permits resume and session state is available
- **THEN** the SDK MUST reconnect using the saved session state instead of starting a brand new session

#### Scenario: Stop after retry exhaustion
- **WHEN** reconnect attempts exceed the configured retry limit
- **THEN** the SDK MUST surface a terminal connection failure to the caller
