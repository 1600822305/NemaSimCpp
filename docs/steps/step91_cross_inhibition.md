# Step 91: B-class + DA 交叉抑制通路补全

## 动机

Step 90 添加了 VA→DD（后退腹侧→D-class）。本步骤对称补全剩余三条
兴奋性→抑制性 MN 通路：VB→VD、DB→DD、DA→VD，完成全部四条交叉抑制通路。

## 生物学基础

### 交叉抑制机制 (White 1986)
每条兴奋性 MN 在收缩同侧肌肉的同时，激活 D-class 抑制性 MN 来松弛对侧：

| 通路 | 功能 | Step |
|------|------|------|
| VA→DD | 后退腹侧相 → 腹侧松弛 | 90 |
| VB→VD | 前进腹侧相 → 背侧松弛 | 91 |
| DB→DD | 前进背侧相 → 腹侧松弛 | 91 |
| DA→VD | 后退背侧相 → 背侧松弛 | 91 |

### 调优记录
初版使用双重叠映射（每个 MN→1-2 个 D-class），总计+48突触。
导致 speed -65%, muscle work -72%（D/V 互相抵消过强）。
修正为仅保留主要重叠（每个 MN→1 个 D-class），+27 突触，regtest 恢复。

## 实现

### VB→VD（11 条）
每个 VB 兴奋其主要重叠区域的 1 个 VD（权重 1）。

### DB→DD（7 条）
每个 DB 兴奋其主要重叠区域的 1 个 DD（权重 1）。

### DA→VD（9 条）
每个 DA 兴奋其主要重叠区域的 1 个 VD（权重 1）。

## 验证 (3 seeds, 300s, no-toxin)

| Seed | CI | near_food | X disp | 结果 |
|------|-----|-----------|--------|------|
| 42 | 0.236 | 30% | +13.3mm | FOOD ✅ |
| 7 | -0.331 | 20% | +23.0mm | FOOD ✅ |
| 99 | 0.245 | — | +14.7mm | FOOD ✅ |

## 修改文件

| 文件 | 修改内容 |
|------|---------|
| `src/connectome/connectome_builder.cpp` | +27 突触 (VB→VD 11 + DB→DD 7 + DA→VD 9) |
| `src/simulation/regression_test.cpp` | 基线更新: 210/513/171 |

## Regtest

20/20 PASS，神经元 210，突触 513(+27)，间隙连接 171
