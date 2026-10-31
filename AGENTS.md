# CsLearnHub Agent 规则与工程规范

本文件为 Antigravity Agent 在本仓库中的操作与输出守则，所有在此仓库编写或修改的技术文档必须严格遵循。

## 1. 文本图表（ASCII / 盒线图）严格对齐规范

在技术文档中绘制 ASCII / 盒线图（内存排布、流水线拓扑、缓存行分布等）时，必须确保字符级绝对对齐：

1. **中英文视觉宽度差补偿**：
   - 半角 ASCII 字符与 Unicode 盒线符号（`─`, `│`, `┌`, `┐`, `└`, `┘`, `├`, `┤`, `┼`, `═`, `▲`, `▼`）视觉显示宽度为 **1**。
   - 全角中文字符与中文标点显示宽度为 **2**。
   - 计算对齐空白填充时必须满足：`填充空格数 = 目标列宽 - (ASCII字符数 + 汉字数 * 2)`。切勿直接使用代码字符计数 `len()`。
2. **框线垂直闭合与连续性**：
   - 同一组件各行（顶边、内容、隔线、底边）视觉总宽必须严格一致，右侧边界 `│` / `|` 必须完全垂直无锯齿。
   - 垂直连接线（如 `│`、`▲`、`▼`）必须精确对准目标模块或字段的水平视觉中心。
   - 流水线分流/汇聚拓扑（如 `┼` 连线）必须在各并行节点间保持连通性，不可悬空。
3. **内存布局比例对齐**：
   - 字节长度相同的字段，其文本列宽必须等宽。
   - 跨字节合并字段（如 8 字节指针对应两个 4 字节字段）必须精准跨越对应的上层列边界。

## 2. 文档工程标准
- 遵循 `templates/topic-template.md` 规范。
- 保证无锁、并发与网络协议相关的技术实现具备工业级考量（Cache-line 对齐、内存序屏障说明、防 ABA 与 UAF 说明）。

## 3. Mermaid 图表视觉主题规范（极简清爽风 Modern Slate）

为杜绝默认 Mermaid 流程图的粗糙、灰白扁平与无层级问题，所有生成的 Mermaid 图表必须遵循**极简清爽风（Modern Slate / Stripe 风格）**主题规范：

1. **统一初始化配置（Directive Init）**：
   图表开头必须注入 `%%{init: {'theme': 'base', 'themeVariables': { ... }}}%%`，配置现代技术文档字体栈（`Inter, system-ui, -apple-system, sans-serif`）、石板灰边框与连接线（`#64748b`）及清爽蓝强调色：
   ```mermaid
   %%{init: {
     'theme': 'base',
     'themeVariables': {
       'fontFamily': 'Inter, system-ui, -apple-system, sans-serif',
       'fontSize': '14px',
       'primaryColor': '#e0f2fe',
       'primaryTextColor': '#0f172a',
       'primaryBorderColor': '#0284c7',
       'lineColor': '#64748b',
       'secondaryColor': '#f1f5f9',
       'tertiaryColor': '#ffffff',
       'actorBkg': '#f8fafc',
       'actorBorder': '#0284c7',
       'actorTextColor': '#0f172a',
       'actorLineColor': '#94a3b8',
       'signalColor': '#334155',
       'signalTextColor': '#0f172a',
       'labelBoxBkgColor': '#f1f5f9',
       'labelBoxBorderColor': '#cbd5e1',
       'labelTextColor': '#0f172a',
       'noteBkgColor': '#fef3c7',
       'noteBorderColor': '#d97706',
       'noteTextColor': '#78350f',
       'activationBkgColor': '#e0f2fe',
       'activationBorderColor': '#0284c7'
     }
   }}%%
   ```
2. **视觉基调原则（视觉克制，杜绝高饱和彩色块堆叠）**：
   - **主体一致性**：流程图与状态机以石板灰（Slate `#f8fafc` / `#64748b`）为主基调，保持画面干净通透。
   - **克制高亮**：仅关键入口与核心处理节点使用天蓝强调（`fill:#e0f2fe,stroke:#0284c7`），特殊屏障/排空采用温和琥珀色（`fill:#fef3c7,stroke:#d97706`），丢弃/忽略采用柔和灰色（`fill:#f1f5f9,stroke:#cbd5e1`）。
   - **时序图通透性**：时序图依靠清晰的参与者卡片（Actor Cards）与精炼的 `Note over` 表达阶段，避免大面积重色遮挡文本。

## 4. 代码设计与注释规范（秉承《A Philosophy of Software Design》与《Clean Code》共识）

所有在文档中生成、修改或示范的技术代码，必须严格遵循以下工业级共识：

1. **全英文注释原则（English Only for Code Comments）**：
   - 无论技术文档中的嵌入代码块，还是仓库中的实验与工程源码（C/C++、Python、Rust、Go、Shell 等），**代码内部所有注释（行内注释 `//`、块级注释 `/* ... */`、函数与模块 Docstring）必须一律使用专业规范的英文编写，严禁出现任何中文注释**。
   - 文档正文分析与架构解读采用中文，代码实现与代码注释严格保持纯正国际工业级全英文。
2. **注释四分法原则（严禁复述 What，深度解释 Why 与 Invariants）**：
   - **杜绝复述代码（No Noise / Clutter）**：严禁在显而易见的代码旁附加无信息增量的自然语言译文（如在 `uint8_t` 后注明 `(1 Byte)`，在 `return false;` 旁注明 `// queue full`，在赋值操作前添加 `// read entry` 等流水账步骤翻译）。
   - **深度阐述设计决策（Explain the Why）**：注释只写在刀刃上，深入解释“为什么这样设计”、“为何放弃常规直观解法”（例如：为何利用无符号补码溢出回绕来消除空满判断歧义、为何通过首成员 `alignas` 传导实现结构体整体对齐等）。
   - **声明核心不变量与并发契约（Invariants & Memory Order Pairs）**：
     - 在关键数据结构头部集中声明抽象不变量（Invariants）；
     - 在无锁并发代码中，精准标注单核单写者所有权、Acquire-Release 屏障配对关系（Happens-Before Pair）与 Cache Line 独占防伪共享意图。
   - **正确性防线交由断言（Sanity as Assertions）**：能用 `static_assert`（尺寸、对齐、偏移）或运行期 `assert`（2 的幂等前置条件）固化的契约，必须硬编码为断言，而不是仅停留于文字注释。
3. **深模块设计（Deep Modules）**：
   - 追求“小接口，深实现”（Small Interface, Deep Implementation），将并发与硬件底层细节封装在清晰的契约边界之后。
   - 变量命名、函数签名与枚举设计具备高度自解释性，消除用注释解释变量名含义的需求。

## 5. Markdown 渲染防护与代码块防截断规范（Anti-Premature-Closure）

在与用户对话交互、向用户展示草稿（Draft Block）或生成复杂文档时，必须杜绝代码块嵌套导致的截断与排版坍塌：

1. **多级反引号嵌套原则（4+ Backticks for Outer Blocks）**：
   - 当在回复中需要用代码块包裹展示“内部本身含有代码块（` ``` `，如 ASCII 图、C 代码、Mermaid 等）”的 Markdown 片段时，**外层必须使用至少四道反引号（````markdown ... ````）包裹**。
   - **严禁**内外层同时使用三道反引号（` ``` `）。若内层代码块的闭合标记提前终结了外层包裹，会导致内层后续的 ASCII 盒线图、缩进、换行全部泄露为普通 HTML/Markdown 正文，触发空白折叠压缩，使原本对齐的图表发生灾难性扭曲和乱码。
2. **草稿展示保真度**：
   - 优先通过清晰的引用或分段展示正文，避免为了“形式上的打包”而在外层滥套代码块。
   - 涉及 KaTeX 数学公式时，注意反斜杠与 `$` 符号在不同渲染模式下的转义，杜绝公式溢出影响周围文本排版。

## 6. 专业术语纯英文原则（Terminological Purity: No Redundant Translations）

在编写技术文档、架构设计与工程分析时，对于计算机体系结构、操作系统、无锁并发与高性能网络等专业领域的标准术语（如 Cache Line, False Sharing, Fast Path, Slow Path, In-band Sentinel, Drain Barrier, Memory Order, Acquire-Release, Quiescent State, Epoch, Ring Buffer, UAF, ABA, RFO, Spillover Buffer 等）：

1. **直接使用原生纯正英文**：
   - **严禁**在英文专业术语后画蛇添足地附加中文括号翻译（例如严禁写成 `False Sharing（伪共享）`、`Fast Path（快路径）`、`Quiescent State（静止状态）`、`Cache Line Bouncing（缓存行颠簸）`，必须直接写作 `False Sharing`、`Fast Path`、`Quiescent State`、`Cache Line Bouncing`）。
2. **拒绝生硬翻译腔与繁冗修饰**：
   - 用精练自然的中文承接行文逻辑，技术实体与机制名称纯英文展现，杜绝一切冗余的“英汉对照字典式”啰嗦写法，保持国际顶级工业级白皮书的利落、专业与高信息密度。

## Agent skills

### Issue tracker

GitHub Issues. See `docs/agents/issue-tracker.md`.

### Triage labels

Canonical 5-role defaults. See `docs/agents/triage-labels.md`.

### Topic generation

`/to-topic`: Synthesize conversation insights into a publication-grade topic document adhering to repo standards.

### Domain docs

single-context. See `docs/agents/domain.md`.
