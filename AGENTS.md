# Zephyr RTOS workspace

> 副本。本檔案與 `../../AGENTS.md` 內容相同，因為 `apps/HMI/` 是獨立的 git repo，
> opencode 往上找 AGENTS.md 時會在這裡的 worktree root 停住，讀不到 workspace 根目錄那份。
> 修改時請同步更新兩份。

West workspace 根目錄是 `/home/peter996/Projects/zephyrproject`，Zephyr 版本 4.4.99。
`apps/` 透過 symlink `modules/hmi -> ../apps/HMI` 接進 west（手動建立，不在 `.west/config` 裡）。

## 查 Zephyr API / Kconfig / Devicetree 設定時

先用 `zephyr-docs` MCP 工具（`search_zephyr_knowledge_sources`）查證，
不要憑記憶猜 `CONFIG_*` 名稱或 API 簽名。引用時附上來源 URL。

### 這套知識庫的限制（已實測）

`https://zephyrproject.mcp.kapa.ai` 混合索引了官方文件**與 GitHub issues**。
問「怎麼設定 / 怎麼用」時，排名最高的結果**經常是 bug issue 討論串**而不是文件
（實例：問「how to enable and configure SPI」→ 回傳 PocketBeagle 2 的 closed issue）。

因此：

- **看 URL 判斷來源**：`docs.zephyrproject.org` = 官方文件（可信）；
  `github.com/.../issues` = 使用者經驗（僅供參考）。
- **精確數值一律 grep 本機 tree** — 這才是你實際編譯的版本：

  ```bash
  # Kconfig symbol 定義、預設值、depends on
  fd -e Kconfig . ~/Projects/zephyrproject/zephyr | rg <subsys>
  rg -A20 'config <SYMBOL_NAME>' ~/Projects/zephyrproject/zephyr

  # Devicetree binding schema
  rg -l '<vendor>,<device>' ~/Projects/zephyrproject/zephyr/dts/bindings

  # 官方範例
  fd . ~/Projects/zephyrproject/zephyr/samples | rg <keyword>
  ```

- **不要**把 MCP 回傳的結論直接當事實寫進 commit message 或程式碼註解。
  Zephyr 自己的 `zephyr/AGENTS.md` 也寫明：*"its answers are generated and must be
  verified against the tree."*
