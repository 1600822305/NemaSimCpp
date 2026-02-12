# Step 39-42: 运动扩展与行为整合

> 本文档为中文档，合并 Step 39-42 的完整一级内容。

---

## Step 39: 运动神经元扩展 — 完整 B/A/D 覆盖

- **扩展**: DB 3→7, VB 3→7, DA 3→5, VA 3→5, DD 3→5, VD 3→5, AS 5→7 (+18 神经元)
- **连续覆盖**: B 类 7 单元 tile seg 4-42 无间隙 (之前 3 单元有大跳跃)
- **突触扩展**: AVA→DA/VA 5个, AVB→DB/VB 7个, DD↔VD 5对, DD⊣AS, DB↔AS gap, DVA→DB/VB 7个, AVE→DA 5个
- **本体感觉**: B 类 7 级顺序接力 (DB01→DB02→...→DB07), A 类 5 级同步
- **结果**: regtest 17 pass (3 连续); muscle work 0.316→0.338, curv stability 2.0→0.6 Hz
- **REF**: White 1986, Haspel 2010, Wen 2012, Gao & Zhen 2018
- **文档**: [step39_motor_expansion.md](step39_motor_expansion.md)

---

## Step 40: 稳定性审计 — 5-HT 稀释修复 + 参数校准

- **5-HT 稀释 bug**: release_drive 分母从 total sources → active sources，5-HT 0.34→0.73
- **diag CLI 覆盖 bug**: 硬编码默认值覆盖 header 默认值，参数修改对 diag 不可见
- **pulse_amp**: 60→50 (omega ratio 对此不敏感，5-HT 修复是主因)
- **regtest baselines**: SMDVL swing 55→45/65%, heading 10→5/60%, omega 3→1/200%
- **10-seed 结果**: speed std=0.001, 5-HT std=0.003, omega=0.44±0.11, wave 8/8 GOOD
- **清理**: 3 个旧 sweep 脚本 → 1 个通用 sweep.ps1
- **文档**: [step40_stability_audit.md](step40_stability_audit.md)

---

## Step 41: 行为整合 — 后退运动 + 觅食状态调制 + Warmup

- **后退运动**: pirouette reversal 覆盖 command neuron balance → `body_.set_locomotion_state(0,1)` 强制后退
  - 后退速度 60% (Fang-Yen 2010), 曲率偏置仅限前进阶段 (Iino 2009)
- **NSM/CEP 阈值修正**: half_max 0.1→0.5，防止食物远处 (10mm) 产生虚假 5-HT/DA 释放
- **5-HT 释放阈值**: 0.3→0.5，防止 ADF 基线活性膨胀 off-food 5-HT
- **速度调制增强**: 5-HT -40% (Sawin 2000), DA -30% (basal slowing), OA +35% (补偿)
- **Weathervane 5-HT 调制**: off-food(5-HT≈0)→全额, on-food(5-HT≈0.7)→40% SMD fraction
- **Omega 方向**: 体姿信号 (SMD 相位) + 梯度信号共同决定 omega L/R bias
- **Warmup**: 50 步网络平衡后重置神经调质浓度，消除初始瞬态
- **reset_transducers()**: 环境变化后重置化学感觉转导器

---

## Step 42: Cook 2019 连接组校准 + 性能优化 + Fitness 框架

- **连接组校准 (Cook 2019 EM)**: AVD→AVA 1→2, AIB→AVA 3/3→2/1, AIY→RIA 4→5
- **AVE→RIV 删除**: Cook 2019 无此连接，此前导致 RIV tonic 激活破坏 omega
- **RIA↔RIV 负反馈环路**: RIA→RIV(ACh兴奋) + RIV→RIA(GABA抑制) + RIV↔RIV gap(4)
  - 自限制振荡: RIA 兴奋 RIV → RIV 抑制 RIA → 周期性，TA 深度抑制后 rebound = omega
- **参数**: pulse_amp 50, as_factor 1.7 (环路补偿)
- **性能优化**: 缓存 awc_pref(消除 3 亿次/300s 字符串操作), 缓存 10 个 neuron ID(消除 1080 万次哈希查找), 缓存 6 个 typed 指针(消除 360 万次 dynamic_cast), 预索引 AWC/ASER 学习突触
- **Fitness 框架**: `--fitness` CLI, SimMetrics 结构体, 4 seeds × 3 scenarios 自动评估
  - f = 10·CI - 5·max(0,CI_toxic) - 3·|ω-0.65| - 3·|DV-1| - 2·|spd-0.18| + 2·near_food
- **regtest**: 17 pass, 0 FAIL
- **文档**: [step42_connectome_calibration.md](step42_connectome_calibration.md)

---

## 本组总结

| 指标 | 之前 | 之后 |
|------|------|------|
| 运动神经元 | 3×B/A/D | **7×B, 5×A/D** (+18) |
| 连接组 | 初始构建 | **Cook 2019 EM 校准** |
| Omega 回路 | AIB→RIV | **RIA↔RIV 负反馈环路** |
| 工具 | regtest | +**fitness 框架** (4-seed×3-scenario) |
| 5-HT bug | 稀释 0.34 | **修复→0.73** |
