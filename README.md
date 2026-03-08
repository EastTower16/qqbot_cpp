# qqbot_cpp

一个基于 C++17 的 QQ Bot SDK 实现，当前已接入真实网络依赖并支持：

- OpenAPI 请求
- WebSocket 网关连接
- 频道消息 API
- 私信 / C2C / 群聊消息 API
- C2C / 群聊文件上传接口
- C2C / 群聊 / 私信 / 频道消息 reply 风格封装

当前工程使用：

- `CMake`
- `vcpkg`
- `CURL`
- `OpenSSL`
- `boost-beast`
- `nlohmann-json`

## 目录结构

```text
qqbot_cpp/
├─ CMakeLists.txt
├─ src/
│  ├─ common/
│  ├─ transport/
│  ├─ openapi/
│  ├─ websocket/
│  ├─ sdk/
│  ├─ message/
│  └─ examples/
├─ bot-node-sdk/
└─ botpy/
```

## 依赖要求

已在工程中按 `vcpkg` 方式接入：

- `openssl`
- `curl`
- `nlohmann-json`

其中：

- `OpenSSL`
- `CURL`

当前仍建议通过 `vcpkg` 提供。

`nlohmann-json` 现在支持两种来源：

- 如果本机 / `vcpkg` 中可用，优先使用本地包
- 如果找不到，`CMake` 会自动通过 `FetchContent` 从 GitHub 拉取

你的环境里如果已经安装了 `vcpkg`，并且路径为：

```text
G:\vcpkg
```

则当前 `CMakeLists.txt` 会优先使用：

```text
G:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

默认 triplet：

```text
x64-windows-static
```

## 编译

### 1. 生成工程

在项目根目录执行：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64  -DCMAKE_TOOLCHAIN_FILE=G:\vcpkg\scripts\buildsystems\vcpkg.cmake  -DVCPKG_TARGET_TRIPLET=x64-windows-static
```

如果你本机没有安装 `nlohmann-json`，现在也可以直接继续配置；`CMake` 会在缺失时自动拉取该依赖。

### 2. 编译 Release

```powershell
cmake --build build --config Release
```



## 可执行目标

当前会生成这些目标：

- `qqbot_sdk`
  - SDK 静态库

- `qqbot_sdk_smoke`
  - smoke test / 基础构建验证

- `qqbot_sdk_api_example`
  - OpenAPI 基础调用示例

- `qqbot_sdk_message_reply_example`
  - 收消息并回复示例
  - 覆盖：频道 / 私信 / C2C / 群聊
  - 支持附件信息打印
  - 支持 C2C / 群聊文件上传 + 富媒体发送链路示例

## 示例一：基础 OpenAPI 示例

文件：

```text
src/examples/api_example.cpp
```

这个示例展示：

- 创建 `OpenAPIV1Client`
- 获取机器人自身信息
- 获取网关信息
- 获取加入的 guild 列表
- 创建私信会话
- 发送私信消息

### 运行前环境变量

至少设置：

```powershell
$env:QQBOT_APP_ID="你的 AppID"
$env:QQBOT_CLIENT_SECRET="你的 ClientSecret"
```

兼容旧版 SDK 的固定 Token 模式仍保留 fallback：

```powershell
$env:QQBOT_TOKEN="旧 Token，仅兼容使用"
```

可选：

```powershell
$env:QQBOT_SANDBOX="true"
$env:QQBOT_DM_USER_ID="目标用户ID"
$env:QQBOT_DM_GUILD_ID="私信会话guild_id"
```

### 运行

```powershell
.\build\Release\qqbot_sdk_api_example.exe
```

## 示例二：收消息并回复

文件：

```text
src/examples/message_reply_example.cpp
```

这个示例当前会处理这些事件：

- `DIRECT_MESSAGE_CREATE`
- `C2C_MESSAGE_CREATE`
- `GROUP_AT_MESSAGE_CREATE`
- `GROUP_MSG_RECEIVE`
- `MESSAGE_CREATE`
- `AT_MESSAGE_CREATE`

### 支持能力

- 收到文本消息后按触发词回复
- 解析并打印附件信息
- 对 C2C / 群聊执行：
  - 文件上传
  - 富媒体消息发送

如果平台已经给你的机器人开通普通群消息接收能力，那么 `GROUP_MSG_RECEIVE` 也会进入同一套群消息处理逻辑。

### 运行前环境变量

至少设置：

```powershell
$env:QQBOT_APP_ID="你的 AppID"
$env:QQBOT_CLIENT_SECRET="你的 ClientSecret"
```

兼容旧版 SDK 的固定 Token 模式仍保留 fallback：

```powershell
$env:QQBOT_TOKEN="旧 Token，仅兼容使用"
```

建议设置：

```powershell
$env:QQBOT_SANDBOX="true"
$env:QQBOT_REPLY_TRIGGER="/ping"
$env:QQBOT_REPLY_TEXT="pong"
```

可选：

```powershell
$env:QQBOT_MEDIA_URL="https://example.com/demo.png"
$env:QQBOT_INTENTS="1107296256"
$env:QQBOT_SKIP_TLS_VERIFY="true"
```

### 运行

```powershell
.\build\Release\qqbot_sdk_message_reply_example.exe
```

## 鉴权说明

当前 SDK 优先使用新版 `AccessToken` 鉴权流程：

1. 读取：
   - `QQBOT_APP_ID`
   - `QQBOT_CLIENT_SECRET`
2. 自动请求：
   - `https://bots.qq.com/app/getAppAccessToken`
3. 自动缓存 `access_token`
4. HTTP / WebSocket 统一使用该 token

如果你仍设置了旧的：

```powershell
$env:QQBOT_TOKEN="..."
```

当前 SDK 会把它当作兼容 fallback，但平台已经不推荐继续使用这种方式。

## TLS 握手说明

如果你在 Windows + OpenSSL/vcpkg 环境下遇到：

```text
handshake: certificate verify failed
```

可以临时设置：

```powershell
$env:QQBOT_SKIP_TLS_VERIFY="true"
```

这会跳过 WebSocket TLS 证书校验，适合本地联调排查。

## `QQBOT_INTENTS` 说明

`QQBOT_INTENTS` 是一个整数位掩码，用来告诉网关你要订阅哪些事件。

当前示例如果你不设置它，会使用代码里的默认值：

```cpp
qqbot::common::intents::kMessageReplyExample
```

它等价于：

```cpp
qqbot::common::intents::kDirectMessage
| qqbot::common::intents::kPublicMessages
| qqbot::common::intents::kPublicGuildMessages
```

十进制值为：

```text
1107296256
```

你也可以直接显式设置，例如：

```powershell
$env:QQBOT_INTENTS="1107296256"
```

如果你出现下面这种情况：

- WebSocket 已连接
- 但收不到 C2C / 群聊 / 私信消息事件

优先检查：

- 机器人后台是否开通对应事件
- `intents` 是否配置正确

### 已提供的 intents 常量

- `qqbot::common::intents::kNone`
- `qqbot::common::intents::kGuilds`
- `qqbot::common::intents::kGuildMembers`
- `qqbot::common::intents::kGuildMessages`
- `qqbot::common::intents::kGuildMessageReactions`
- `qqbot::common::intents::kDirectMessage`
- `qqbot::common::intents::kPublicMessages`
- `qqbot::common::intents::kInteraction`
- `qqbot::common::intents::kMessageAudit`
- `qqbot::common::intents::kForumsEvent`
- `qqbot::common::intents::kAudioAction`
- `qqbot::common::intents::kPublicGuildMessages`
- `qqbot::common::intents::kAudioOrLiveChannelMember`
- `qqbot::common::intents::kOpenForumEvent`
- `qqbot::common::intents::kDefault`
- `qqbot::common::intents::kAll`
- `qqbot::common::intents::kMessageReplyExample`

### intents 组合 helper

```cpp
using namespace qqbot::common;

const int intents = intents::Combine({
    intents::kDirectMessage,
    intents::kPublicMessages,
    intents::kPublicGuildMessages,
});

const bool has_public_messages = intents::Has(intents, intents::kPublicMessages);
```

## 消息事件常量

当前 `message` 模块也提供了具名事件常量，避免在业务代码里散落硬编码字符串。

例如：

- `qqbot::message::kMessageCreateEvent`
- `qqbot::message::kAtMessageCreateEvent`
- `qqbot::message::kDirectMessageCreateEvent`
- `qqbot::message::kC2CMessageCreateEvent`
- `qqbot::message::kGroupAtMessageCreateEvent`
- `qqbot::message::kMessageDeleteEvent`
- `qqbot::message::kDirectMessageDeleteEvent`
- `qqbot::message::kPublicMessageDeleteEvent`
- `qqbot::message::kGroupAddRobotEvent`
- `qqbot::message::kGroupDeleteRobotEvent`
- `qqbot::message::kGroupMessageRejectEvent`
- `qqbot::message::kGroupMessageReceiveEvent`
- `qqbot::message::kFriendAddEvent`
- `qqbot::message::kFriendDeleteEvent`
- `qqbot::message::kC2CMessageRejectEvent`
- `qqbot::message::kC2CMessageReceiveEvent`

## SDK 使用示例

### 1. 创建 OpenAPI 客户端

```cpp
qqbot::common::BotConfig config;
config.app_id = "app_id";
config.token = "token";
config.sandbox = true;

auto client = qqbot::sdk::CreateOpenAPI(config);
auto v1 = std::dynamic_pointer_cast<qqbot::openapi::v1::OpenAPIV1Client>(client);
```

### 2. 发送频道消息

```cpp
v1->PostMessage("channel-id", {{"content", "hello"}});
```

### 3. 发送私信消息

```cpp
v1->PostDirectMessage("dm-guild-id", {{"content", "hello"}});
```

### 4. 发送 C2C 消息

```cpp
v1->PostC2CMessage("user-openid", {
    {"content", "hello"},
    {"msg_type", 0},
    {"msg_id", "source-msg-id"}
});
```

### 5. 发送群聊消息

```cpp
v1->PostGroupMessage("group-openid", {
    {"content", "hello"},
    {"msg_type", 0},
    {"msg_id", "source-msg-id"}
});
```

### 6. 上传 C2C 图片/文件

```cpp
v1->PostC2CFile("user-openid", {
    {"file_type", 1},
    {"url", "https://example.com/a.png"},
    {"srv_send_msg", false}
});
```

### 7. 上传群聊图片/文件

```cpp
v1->PostGroupFile("group-openid", {
    {"file_type", 1},
    {"url", "https://example.com/a.png"},
    {"srv_send_msg", false}
});
```

## reply 风格封装

当前 `OpenAPIV1Client` 已提供：

- `Reply(const message::ChannelMessage&, ...)`
- `Reply(const message::DirectMessage&, ...)`
- `Reply(const message::C2CMessage&, ...)`
- `Reply(const message::GroupMessage&, ...)`

作用：

- 自动带上来源消息的 `msg_id`
- C2C / 群聊自动补默认 `msg_type` / `msg_seq`

例如：

```cpp
const auto message = qqbot::message::ParseC2CMessage(event);
v1->Reply(message, {
    {"content", "reply"},
    {"msg_type", 0}
});
```

## 附件 / 图片接收

所有接收到的消息对象都可以从：

```cpp
message.attachments
```

读取附件列表。

单个附件包含：

- `filename`
- `content_type`
- `url`
- `width`
- `height`
- `size`

例如：

```cpp
for (const auto& attachment : message.attachments) {
    std::cout << attachment.filename << " " << attachment.url << std::endl;
}
```

## 当前已实现范围

已完成：

- 频道 OpenAPI
- 私信 OpenAPI
- WebSocket 网关连接
- C2C 消息发送
- 群聊消息发送
- C2C / 群聊文件上传接口
- C2C / 群聊 / 私信 / 频道 reply helper
- 附件解析
- 示例程序与 smoke test

