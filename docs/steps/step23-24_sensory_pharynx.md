# Step 23-24: 新感觉模态与咽部系统

> 本文档为中文档，合并 Step 23-24 + Post-24 修复的完整一级内容。
> 详细子文档见各 Step 链接。

---

## 概述

本组验证了架构通用性（新感觉模态无需修改下游回路）并替换了关键占位符（距离→真实咽部泵食）。

---

## Step 23: 温度趋性 (Thermotaxis) ✅ (2026-02-10)

> 详细文档: [step23_thermotaxis.md](step23_thermotaxis.md)

新感觉模态接入已有回路 — 验证架构通用性:
- **AFD L/R**: 温度感觉神经元 (谷氨酸能), Mori & Ohshima 1995
- **AFD→AIY** (3 sections): 共享 AIY→RIA→SMD 下游通路 (与趋化相同!)
- **AFD→AIZ** (2 sections): 冷趋性分支 (Mori 1995 — AIZ 消融→嗜热)
- **ThermoTransducer**: 培养温度记忆 Tc (tau=120s), dT 响应 (gain=60pA/°C)
- **温度场**: 线性梯度 0.5°C/mm, 中心 20°C (7.5°C~32.5°C)
- **饱食调制**: 已通过 AWC-AIA 通路自动实现 (eLife 2021 Hawk — INS-1 肠脑信号)
- **结果**: AFD 活跃 (-39.9/-45.8 mV), 不破坏趋化 (CI 正常)
- **架构验证**: 新感觉神经元接入共享节点 AIY, 无需修改下游回路
- **REF**: Mori 1995, Clark 2006, Luo 2014 PNAS, eLife 2021 Hawk

---

## Step 24: 咽部泵食系统 (Pharyngeal Pumping) ✅ (2026-02-10)

> 详细文档: [step24_pharyngeal_pump.md](step24_pharyngeal_pump.md)

替换占位符 satiety (`dist<3mm→sat+=dt/τ`) 为真实咽部泵食机制:
- **9 个咽部神经元**: MC L/R (ACh起搏器), M3 L/R (Glu松弛计时), M4 (峡部蠕动), I1 L/R (桥梁), RIP L/R (咽外桥梁)
- **PharyngealPump**: 4相状态机 (REST→E→P→R), MC调制不应期 (800ms→200ms = 1-4 Hz)
- **5-HT→MC SER-7**: +15pA 兴奋 (Song & Avery 2012), OA→MC: -10pA 抑制
- **真实进食**: pump_event × food_conc × 0.006 → satiety (泵频~2-3Hz, ~800次/300s)
- **5-HT正反馈环**: food→NSM→5-HT→MC→↑pump→↑intake→↑sat→NSM↓
- **结果**: CI≈0.4-0.5, satiety振荡0.4-0.55, FOOD↔TEMP切换正常
- **REF**: Avery (WormBook 2012), Raizen & Avery 1994, Song & Avery 2012 eLife

---

## Post-24 修复: Pirouette 调制 + SMD 修复 + 回归测试工具

- Pirouette dC/dt sigmoid + 梯度/食物密度分离 + omega 方向/持续时间修复 → CI=0.746
- SMD 振荡器修复 (移除 ±200pA omega 注入) → SMD 振幅 222→115mV
- **regtest 工具**: 30s 基线对比 + 电流溯源 + 注入检测

---

## 本组总结

| 指标 | 之前 | 之后 |
|------|------|------|
| 神经元 | 72 | **84** (+12: AFD×2 + 咽部 9 + Post-24 调整) |
| satiety 来源 | 距离占位符 | 真实咽部泵食 |
| 感觉模态 | 化学 | 化学 + **温度** |
| CI | 0.43 | **0.746** (Post-24 修复后) |

### 参考文献

- Mori 1995, Clark 2006, Luo 2014 PNAS — 温度趋性
- Avery WormBook 2012, Song & Avery 2012 eLife — 咽部泵食
