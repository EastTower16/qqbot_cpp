# WebSocketClient 状态机说明

本文档描述 `qqbot::websocket::WebSocketClient` 在当前实现下的状态机、线程职责、关键标志位，以及 session 持久化语义。目标是帮助后续维护者在修改 `Connect()`、`Disconnect()`、`HandleClose()`、`ControlLoop()`、`TryReconnect()` 时保持行为一致。

## 1. 设计目标

当前实现采用以下原则：

- `reconnect_thread` 是常驻控制线程，只负责等待和驱动重连。
- `read_thread` 负责读取 gateway 消息并分发事件。
- `heartbeat_thread` 负责心跳发送和心跳超时检测。
- `Connect()` 负责建立一次连接，不负责回收常驻控制线程。
- `Disconnect()` 负责显式停机，并统一结束控制线程、读线程、心跳线程。
- 自动重连只允许由 `ControlLoop()` 驱动，不允许外部在活跃连接阶段重复 `Connect()`。

## 2. 状态定义

`ConnectionPhase` 定义如下：

- `kDisconnected`
  - 当前没有活跃连接。
  - 可以由外部显式调用 `Connect()` 进入下一轮连接。

- `kConnecting`
  - 正在建立 TCP/TLS/WebSocket 连接。
  - 这是外部主动连接的同步建立阶段。

- `kIdentifying`
  - 连接已建立，已收到 `HELLO` 并准备发送或已发送 `IDENTIFY` / `RESUME`。
  - 心跳线程可以启动。

- `kReady`
  - 已完成 `READY` 或 `RESUMED`。
  - 可以正常接收 dispatch、处理心跳 ACK、响应服务端要求的重连。

- `kClosing`
  - 当前连接正在关闭。
  - 该状态既可能来自手动 `Disconnect()`，也可能来自 `HandleClose()` 触发的异常关闭。

- `kReconnecting`
  - 常驻控制线程已经接管，正在按退避策略执行重连。
  - 此状态只能由 `ControlLoop()` 驱动进入。

- `kTerminal`
  - 当前会话被判定为终止态，不继续自动重连。
  - 当前实现下，`kTerminal` 不允许直接再次调用 `Connect()`。

## 3. 允许的主要状态迁移

### 3.1 外部主动连接路径

- `kDisconnected -> kConnecting`
  - 触发：外部调用 `Connect()`。

- `kConnecting -> kIdentifying`
  - 触发：底层 TCP/TLS/WebSocket 建立成功。

- `kIdentifying -> kReady`
  - 触发：收到 `READY` 或 `RESUMED`。

### 3.2 异常关闭与自动重连路径

- `kIdentifying -> kClosing`
- `kReady -> kClosing`
- `kConnecting -> kClosing`
  - 触发：`HandleClose()` 被调用。

- `kClosing -> kReconnecting`
  - 触发：`ScheduleReconnect()` 通知控制线程，`ControlLoop()` 醒来后切换状态。

- `kReconnecting -> kIdentifying`
  - 触发：`TryReconnect()` 内调用 `Connect()` 成功建立连接。

- `kReconnecting -> kTerminal`
  - 触发：终止型关闭码、非可恢复关闭、或重连次数耗尽。

### 3.3 手动断开路径

- 任意活跃状态 -> `kClosing`
  - 触发：外部调用 `Disconnect()`。

- `kClosing -> kDisconnected`
  - 触发：`Disconnect()` 完成底层关闭、线程回收和状态收尾。

## 4. 关键不变量

维护此模块时应始终保持以下不变量：

- `reconnect_thread` 只能是常驻控制线程。
- `Connect()` 不允许 `join()` 控制线程。
- `Disconnect()` 是控制线程生命周期的唯一回收点。
- 活跃连接期间，外部不允许重复调用 `Connect()`。
- 自动重连只能由 `ControlLoop()` 驱动，而不是外部直接模拟。
- `kTerminal` 不是“普通断开”，而是“本轮会话已终止”。

## 5. 线程职责

### 5.1 `reconnect_thread`

职责：

- 执行 `ControlLoop()`。
- 等待 `reconnect_cv`。
- 在 `reconnect_request_pending == true` 时读取重连请求并调用 `TryReconnect()`。

禁止事项：

- 不允许在 `Connect()` 中把它当成一次性线程回收。
- 不允许把业务读写逻辑塞进该线程。

### 5.2 `read_thread`

职责：

- 持续读取 gateway 下行消息。
- 根据 `op` 或事件名触发：
  - `ReceiveHello()`
  - `ReceiveReady()`
  - `ReceiveReconnectSignal()`
  - `HandleClose()`
  - `ReceiveDispatch()`

退出条件：

- `stop_requested == true`
- 或底层读异常并转入 `HandleClose()`

### 5.3 `heartbeat_thread`

职责：

- 按 `heartbeat_interval_ms` 发送心跳。
- 检测 ACK 超时。
- 在心跳超时时调用 `HandleClose(4009, false)`。

退出条件：

- `stop_requested == true`
- 或心跳异常并转入 `HandleClose()`

## 6. 关键标志位语义

### 6.1 `stop_requested`

含义：

- 当前连接相关线程应尽快停止。

使用要点：

- 在 `Disconnect()` 和 `HandleClose()` 中置为 `true`。
- 在开始新的连接建立前可置回 `false`。
- `read_thread` / `heartbeat_thread` 都依赖该标志决定退出。

### 6.2 `manual_disconnect`

含义：

- 当前关闭是否由用户主动触发。

使用要点：

- `Disconnect()` 中置为 `true`。
- 新一轮 `Connect()` 前置回 `false`。
- `HandleClose()` / `ScheduleReconnect()` 发现该标志为 `true` 时，不应再安排自动重连。

### 6.3 `reconnecting`

含义：

- 是否已有一条自动重连流程被激活。

使用要点：

- `HandleClose()` 通过 `compare_exchange_strong` 确保只有一个关闭事件能触发重连流程。
- 在以下场景应复位为 `false`：
  - 连接成功
  - 手动断开
  - 终止态关闭
  - 非可恢复关闭
  - 重连耗尽
  - 手动取消重连

### 6.4 `reconnect_request_pending`

含义：

- 是否存在等待控制线程处理的重连请求。

使用要点：

- `HandleClose()` 中通过 `StoreReconnectRequest()` 写入。
- `ControlLoop()` 等待该标志唤醒。
- `TryReconnect()` 从中取出关闭码与 resumable 信息。

### 6.5 `control_loop_running`

含义：

- 常驻控制线程是否应继续运行。

使用要点：

- 构造后初始化为 `true`。
- `Disconnect()` 中置为 `false`，通知控制线程退出。
- 当控制线程已结束且后续需要再次连接时，`Connect()` 可以重新启动新的控制线程。

## 7. Connect 语义

### 7.1 允许进入的状态

当前实现只允许 `Connect()` 从以下状态进入：

- `kDisconnected`
- `kReconnecting`

其中：

- `kDisconnected` 表示外部主动发起一轮新的连接。
- `kReconnecting` 表示由控制线程驱动的自动重连尝试。

### 7.2 不允许进入的状态

以下状态下外部再次调用 `Connect()` 属于非法使用：

- `kConnecting`
- `kIdentifying`
- `kReady`
- `kClosing`
- `kTerminal`

原因：

- 在活跃连接阶段重复 `Connect()`，会与现有读线程和心跳线程生命周期冲突。
- `kTerminal` 的语义是本轮会话已经终止，不是普通可重试断开。

### 7.3 Connect 异常处理

若 `Connect()` 在建立连接过程中抛异常：

- 如果当前是外部主动连接：状态回落到 `kDisconnected`
- 如果当前是自动重连：状态保持在 `kReconnecting`

这样可以保证：

- 外部观察到的状态与当前控制权一致
- `TryReconnect()` 可以继续按退避策略重试

## 8. Disconnect 语义

### 8.1 职责

`Disconnect()` 的职责是显式停机，而不是协议级 session 失效。

它负责：

- 标记 `manual_disconnect = true`
- 标记 `stop_requested = true`
- 停止控制线程循环
- 关闭底层 websocket
- 回收 `heartbeat_thread`
- 回收 `read_thread`
- 回收 `reconnect_thread`
- 最终切换到 `kDisconnected`

### 8.2 session 语义

当前实现下，手动 `Disconnect()` 不再清理持久化 session。

这表示：

- 用户主动断开并不等于 session 无效
- 后续若重新建立连接，且本地 session 仍在有效期内，则可以尝试 `RESUME`

## 9. HandleClose 语义

### 9.1 适用范围

`HandleClose()` 用于处理非手动断开的异常关闭、协议关闭、心跳超时、读写异常等情况。

### 9.2 核心行为

- 忽略 `manual_disconnect == true` 的情况
- 忽略不允许处理关闭事件的状态
- 通过 `reconnecting` CAS 防止多条关闭路径同时触发重连
- 记录关闭码与 resumable 信息
- 切到 `kClosing`
- 通知控制线程接管后续重连流程

### 9.3 为什么不直接在 HandleClose 里重连

因为当前设计要求：

- 关闭处理线程只负责报告关闭事实
- 常驻控制线程统一负责重连节奏、退避和终止决策

这样可以避免：

- 读线程直接重连
- 心跳线程直接重连
- 多个线程同时抢占重连控制权

## 10. Terminal 语义

`kTerminal` 表示：

- 当前连接已被判定为不应继续自动重连
- 当前实例不应再直接进入新的连接尝试

产生该状态的典型场景：

- 终止型关闭码
- 非可恢复关闭
- 达到最大重连次数

维护建议：

- 如果未来需要支持“从 terminal 显式恢复”，应新增明确的复位 API
- 不建议直接放宽 `Connect()` 让它从 `kTerminal` 进入，否则 terminal 与 disconnected 的语义会重新混淆

## 11. session 持久化语义

### 11.1 会保存 session 的场景

- 收到 `READY`
- 收到 `RESUMED`
- 收到正常 dispatch 并更新 sequence
- mock 连接成功且有有效 session

### 11.2 会清理 session 的场景

当前只应在“协议要求必须重新 Identify”的路径清理 session，例如：

- `ResetSessionForIdentify()`

典型触发原因包括：

- session 已失效
- seq 无效
- token 必须刷新
- 服务端要求重新走 identify 流程

### 11.3 不清理 session 的场景

- 手动 `Disconnect()`
- 可恢复的临时断线
- 正常的自动重连尝试

## 12. 维护时最容易踩坑的点

- 不要在 `Connect()` 中回收 `reconnect_thread`。
- 不要允许活跃状态下重复进入 `Connect()`。
- 不要让读线程或心跳线程自己发起完整重连流程。
- 不要把 `kTerminal` 当成普通断开态。
- 不要在手动 `Disconnect()` 中顺手清空 session，除非协议语义发生变化。
- 如果修改了 `HandleClose()`、`ScheduleReconnect()`、`ControlLoop()` 中任意一个，必须联动检查另外两个函数。

## 13. 推荐回归验证点

后续如果继续改这个模块，至少应验证以下场景：

- 首次连接成功
- `READY` 后正常 dispatch
- 服务端下发 `RECONNECT`，客户端自动恢复
- 心跳超时触发关闭并自动重连
- 连续重连失败直到进入 `kTerminal`
- 手动 `Disconnect()` 后线程全部退出
- 手动 `Disconnect()` 后重新 `Connect()`，若 session 仍有效则优先 `RESUME`
- 活跃连接阶段重复调用 `Connect()` 会被拒绝
