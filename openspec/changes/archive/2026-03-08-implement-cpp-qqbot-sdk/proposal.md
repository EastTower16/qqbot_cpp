## Why

当前仓库只有 `bot-node-sdk` 作为参考实现，而目标是交付一个可直接放在 `src` 目录中的 C++ 版 QQBot SDK。现在先通过 OpenSpec 明确能力边界、架构分层和实施步骤，可以避免后续在 HTTP/OpenAPI、鉴权、WebSocket 事件流和 C++ 工程组织上反复返工。

## What Changes

- 新增一个 C++ QQBot SDK 的总体提案，约束代码落在 `src/` 目录。
- 定义与 `bot-node-sdk` 对齐的核心能力：统一配置、OpenAPI 请求封装、版本化 API 聚合入口、WebSocket 会话连接与事件分发。
- 规划 C++ SDK 的目录结构、模块职责和对外公开接口，便于后续逐步补齐各业务 API。
- 明确初始实现优先级，先完成可用骨架与基础通信能力，再扩展更多频道/私信/成员等细分接口。

## Capabilities

### New Capabilities
- `cpp-openapi-client`: 定义 C++ SDK 的统一配置、鉴权、基础 HTTP 请求与版本化 OpenAPI 聚合客户端能力。
- `cpp-websocket-client`: 定义 C++ SDK 的 WebSocket 会话建立、心跳、重连、事件接收与分发能力。
- `cpp-sdk-layout`: 定义 `src/` 下 C++ SDK 的工程布局、公共导出入口与模块边界。

### Modified Capabilities
- None.

## Impact

- 影响目录：`src/`、`openspec/changes/implement-cpp-qqbot-sdk/`。
- 参考实现：`bot-node-sdk/src` 下的 `bot.ts`、`openapi/`、`client/`、`types/`。
- 预期新增内容：C++ 头文件与源码文件、统一配置与错误模型、HTTP/WebSocket 抽象、事件模型。
- 依赖影响：后续实现阶段可能需要引入 C++ HTTP、JSON 和 WebSocket 相关库，并统一构建方式。
