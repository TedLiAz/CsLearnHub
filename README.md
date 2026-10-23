# 🧠 CsLearnHub
> **打造硬核计算机科学认知体系的个人技术沉淀库与数字花园 (Digital Garden)**  
> 坚持“**深度提问 $\to$ 业界主流方案横向剖析 $\to$ 架构拓扑与时序图解 $\to$ 工业级实现 $\to$ 自我反思拷问**”的闭环，拒绝浅尝辄止，专注底层系统、并发工程、数据面与体系结构。

---

## 🗺️ 知识体系地图与索引

| 模块分类 | 核心研究方向 | 当前已收录专题 |
| :--- | :--- | :--- |
| **[01-computer-architecture](./01-computer-architecture/)** | CPU 缓存一致性 (MESI)、内存屏障 (Memory Barriers)、TSO 乱序执行、NUMA 架构 | *规划中* |
| **[02-operating-systems](./02-operating-systems/)** | Linux 内核调度 (CFS)、虚拟内存与 HugeTLB、VFS 与零拷贝 (splice/io_uring) | *规划中* |
| **[03-high-performance-networking](./03-high-performance-networking/)** | 数据面转发、DPDK、无锁队列流水线、内核旁路 (Kernel Bypass)、eBPF/XDP | 1. [高性能通信流水线中的无锁动态生命周期安全机制](./03-high-performance-networking/lockfree-pipeline-lifecycle-safety.md) |
| **[04-concurrency-and-algorithms](./04-concurrency-and-algorithms/)** | 无锁数据结构 (Lock-Free)、RCU/QSBR/Hazard Pointer、分布式共识 (Raft) | *规划中* |
| **[05-hands-on-labs](./05-hands-on-labs/)** | 核心机制的迷你 POC、微基准测试 (Micro-benchmarks)、手写环形缓冲区验证 | *规划中* |

---

## 🛠️ 单篇沉淀规范与模板

每次与 AI 进行深度技术探讨、复盘或学习攻关后，按照以下格式沉淀入库：

- 模板路径：👉 **[`templates/topic-template.md`](./templates/topic-template.md)**
- 每篇文档必备要素：
  1. **YAML Frontmatter**：包含 `title`, `date`, `category`, `tags`, `difficulty`, `summary`，保障全站检索与元数据提取。
  2. **痛点与本质矛盾**：明确业务场景极限与常规方案的失效原因。
  3. **决策对比矩阵**：横向对比业界顶级项目（DPDK / Linux 内核 / VPP / Folly / Seastar 等）的取舍（Trade-offs）。
  4. **可视化图解**：使用 Mermaid 绘制完整的架构图、时序图（Sequence Diagram）与状态转移机。
  5. **工业级实现**：提供现代 C++20 / Rust 风格的实现代码，重点标明 Cache-line 对齐、内存屏障选择与原子操作。
  6. **认知反思 (Self-Grilling)**：列出 3 条核心灵魂拷问与自身思维模型跃迁。

---

## 🖥️ 推荐的本地阅读与建站分享方式

### 1. 本地双链与图谱可视化（Obsidian）
- 本仓库遵循 Obsidian 规范。直接用 **Obsidian** 打开本目录（`CsLearnHub`）。
- 文档间可通过 `[[主题文件名]]` 建立双向链接。
- 点击右上角 **Graph View**，即可直观查看你在体系结构、网络、操作系统之间建立起的 3D 知识互联星系。

### 2. 对外一键生成现代化技术网站（Quartz 4.0）
本仓库完全兼容 **Quartz 4.0**。如果需要将其公开发布为带暗黑模式、毫秒级全文检索、悬浮预览的在线个人知识库：
```bash
# 只要关联 Quartz 并推送到 GitHub Pages，即可零配置上线
npx quartz create
npx quartz build --serve
```

---

## 📌 当前收录精选

- 🔥 [03-high-performance-networking/lockfree-pipeline-lifecycle-safety.md](./03-high-performance-networking/lockfree-pipeline-lifecycle-safety.md)  
  *深入剖析在多线程无锁 Ring Buffer 流水线中，报文千万级 PPS 快路径与 Channel/Session 动态销毁慢路径并发时的安全机制，采用 Drain Barrier + Generation Slot + QSBR 彻底杜绝 UAF 与 Cache-line Bouncing。*
