# AyuGram Next 本地 API

只读接口,用于取本地已缓存的聊天记录,以及 AyuGram 在消息被撤回后保存下来的内容。

**所有数据都来自本地缓存和 `ayudata.db`,服务端不会为响应发起任何网络请求。**

---

## 开启

默认**关闭**。开启方式:

```
设置 → AyuGram → Spy essentials → Local HTTP API
```

打开后同一区域会出现三行:

| 行 | 作用 |
|---|---|
| API port | 显示实际监听地址,点击复制 `http://127.0.0.1:<port>` |
| Copy API token | 显示遮蔽后的 token,点击复制完整值 |
| Reset API token | 生成新 token 并复制,旧的立即失效 |

### 端口

默认 **9527**。若该端口被占用,启动时会**自动向后寻找可用端口**(最多试 20 个),并把找到的端口写回配置 —— 所以设置页显示的地址永远是真实在听的那个。

也可以手动指定,改 `tdata/ayu_settings.json` 的 `localApiPort`(有效范围 1024–65535),重启生效。

### Token

存放在 `tdata/ayu_api_token`,权限 `0600`。首次需要时生成,256 位随机。

之所以不放进 `ayu_settings.json`:那个文件常被整个贴进 issue 求助,而**泄漏 token 等于泄漏全部聊天记录**。

---

## 鉴权

三种方式任选,优先级从上到下:

```bash
curl -H "Authorization: Bearer <token>" ...
curl -H "X-Ayu-Token: <token>" ...
curl "http://127.0.0.1:9527/chats?token=<token>"
```

query 形式方便直接丢给播放器或浏览器,但会留在 shell history 里,脚本中建议用 header。

`/health` 和 `/openapi.json` 免鉴权 —— 它们描述服务本身,不含任何聊天数据。

### 为什么只有 token 还不够

浏览器里的网页同样能向 `127.0.0.1` 发请求。所以除 token 外还有三道拦截:

| 条件 | 结果 | 挡住什么 |
|---|---|---|
| 请求带 `Origin` 头 | 403 | 网页发起的请求(脚本不带此头) |
| `Host` 不是回环地址 | 403 | DNS rebinding |
| `Sec-Fetch-Site` 为跨站 | 403 | 跨站请求 |

响应也不含任何 `Access-Control-Allow-*` 头,因此即使请求发出,网页 JS 也读不到内容。

---

## 端点

### `GET /health`

免鉴权。

```json
{ "status": "ok", "version": "7.0.8", "session": true }
```

`session` 为 `false` 表示没有账号登录,此时数据接口会返回错误。

---

### `GET /docs`

免鉴权。**浏览器直接打开 `http://127.0.0.1:9527/docs`** 就是一个 Swagger UI 页面,可以查看所有端点、参数说明,并直接在线调用测试。

页面顶部有个 token 输入框,填一次会记在浏览器的 localStorage 里,之后所有 "Try it out" 请求都会自动带上。

> 页面的 JS/CSS 从 CDN 加载,首次打开需要联网。这是**浏览器**在取静态资源,API 本身依旧不发起任何网络请求。

---

### `GET /openapi.json`

免鉴权。返回完整的 OpenAPI 3.1 描述,可直接导入 Postman、Insomnia,或喂给能读 OpenAPI 的 AI 工具。

---

### `GET /chats`

| 参数 | 说明 |
|---|---|
| `with_counts=1` | 每个会话附带 `has_deleted`(是否存有被撤回的消息)。每个会话一次数据库探测,量大时略慢 |

```json
{
  "chats": [
    { "id": -1001234567890, "name": "某某群", "type": "supergroup", "username": "somegroup" },
    { "id": 5312846166, "name": "张三", "type": "user" }
  ]
}
```

`id` 是**带符号的 dialog id**:群组和频道为负数,用户为正数。这个值直接作为 `/messages` 的 `peer` 参数。

---

### `GET /messages`

核心端点。合并「已加载进界面的消息」与「数据库里的已删除消息」,按消息 ID 去重。

#### 参数

| 参数 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `peer` | int64 | **必填** | 来自 `/chats` 的 `id` |
| `limit` | int | 200 | 1–5000 |
| `before_id` | int64 | — | 只取 ID 小于此值的(向前翻页) |
| `after_id` | int64 | — | 只取 ID 大于此值的 |
| `since` | unix 秒 | — | 时间下界 |
| `until` | unix 秒 | — | 时间上界 |
| `q` | string | — | 文本子串匹配 |
| `from` | int64 | — | 只看某个发送者 |
| `filter` | enum | `all` | `all` / `deleted` / `media` / `service` |
| `fields` | csv | `basic` | 见下 |
| `resolve_names` | `1` | 关 | 把 `from` 变成含名字的对象 |

#### `fields` 可选值

逗号分隔,默认只返回基础字段。额外字段有额外开销,所以默认不给:

`media` · `entities` · `forward` · `reply` · `reactions` · `views` · `raw_flags` · `all`

#### 用法示例

```bash
TOKEN=$(cat ~/Library/Application\ Support/AyuGram\ Next/tdata/ayu_api_token)
H="Authorization: Bearer $TOKEN"
BASE=http://127.0.0.1:9527

# 喂给 AI 做总结:要名字和回复关系,不要媒体
curl -H "$H" "$BASE/messages?peer=-1001234567890&resolve_names=1&fields=reply&limit=200"

# 只看被撤回的
curl -H "$H" "$BASE/messages?peer=-1001234567890&filter=deleted"

# 昨天到现在
curl -H "$H" "$BASE/messages?peer=-1001234567890&since=$(date -v-1d +%s)"

# 搜关键词
curl -H "$H" "$BASE/messages?peer=-1001234567890&q=开会"

# 完整归档
curl -H "$H" "$BASE/messages?peer=-1001234567890&fields=all&resolve_names=1&limit=5000"
```

#### 返回

```json
{
  "peer": -1001234567890,
  "name": "某某群",
  "messages": [
    {
      "id": 8843,
      "date": 1785838761,
      "from": { "id": 5312846166, "name": "张三", "username": "zhangsan", "type": "user" },
      "text": "明天三点开会",
      "deleted": true,
      "deleted_at": 1785838800,
      "source": "stored",
      "kind": "message",
      "reply_to": null,
      "media": { "type": "photo", "mime": "image/jpeg", "cached": true, "path": "tdata/ayu_media/-1001234567890_8843.jpg" }
    }
  ],
  "warnings": ["..."]
}
```

**`source` 字段很重要:**

- `live` — 来自已加载的聊天记录,元数据完整
- `stored` — 来自已删除消息数据库,部分字段拿不到(见下)

---

### `GET /media`

| 参数 | 说明 |
|---|---|
| `peer` | 必填 |
| `msg_id` | 必填 |

直接返回文件二进制。

```bash
curl -H "$H" -o photo.jpg "$BASE/media?peer=-1001234567890&msg_id=8843"
```

媒体从未缓存过时返回 404 和 JSON 说明,而不是空响应。

**安全设计**:文件路径不接受客户端输入,而是由 `peer` + `msg_id` 重新解析,再校验最终路径确实位于媒体目录内。HTML、SVG、JS 等可执行类型一律降级为 `application/octet-stream` —— 否则这些内容会运行在 API 自身的源上,可以同源读取其他端点,token 就形同虚设了。

---

## 已知限制

这些不是 bug,是数据本身的边界。API 会在 `warnings` 里如实说明。

### 只能返回已加载的消息

`live` 部分来自客户端内存中的聊天记录,也就是**你滚动浏览过的部分**。没打开过的会话几乎是空的。

要取更多必须联网拉取,与"不发起网络请求"的前提冲突。**已删除消息不受此限**,它们在数据库里,能全部返回。

### 已删除消息缺少部分元数据

AyuGram 保存撤回消息时**没有持久化**转发来源和回复关系(源码中相关映射被注释掉了)。因此 `source: "stored"` 的消息:

- `reply_to` 恒为 `null`
- `forward` / `reply` / `reactions` 拿不到
- `from` 只存了 bare id,若该用户从未完整加载过,`name` 为 `null`

`live` 消息则可以提供全部字段。

### 时间与发送者过滤在内存进行

数据库对 `date` 列没有索引,`since` / `until` / `from` 只能在取回数据后过滤。这意味着**先按 `limit` 截断再过滤**,结果可能比预期少。需要精确范围时,配合 `before_id` / `after_id` 分页多取几轮。

### 其他

- `entities` 的 `offset` / `length` 是 **UTF-16 code unit**(响应中的 `entities_encoding` 字段会标明)。Python / JS 按 UTF-8 字节切会错位,中文和 emoji 尤其明显。
- 服务消息(入群、改名等)的文本来自 `notificationText()`,**超过 255 字符会被截断**,此时 `text_truncated` 为 `true`。
- `views` 为 `null` 表示该消息没有浏览计数,不是 0。
- 截图模式下 `reactions` 会返回空。

---

## 与 OpenAPI 工具配合

```bash
# 导入 Postman / Insomnia
curl http://127.0.0.1:9527/openapi.json -o ayugram-api.json

# 生成客户端代码
openapi-generator generate -i http://127.0.0.1:9527/openapi.json -g python -o ./client
```

Schema 免鉴权,但实际调用数据接口时仍需在工具里配置 Bearer token。
