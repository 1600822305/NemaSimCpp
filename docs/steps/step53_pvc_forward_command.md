# Step 53: PVC — 前进指令中间神经元

## 动机

PVC 是秀丽隐杆线虫五大运动指令中间神经元之一 (AVA, AVB, AVD, AVE, **PVC**)。
它是前进运动的关键中继节点：接收后触觉 (PLM)、趋化前进 (AIY)、本体感觉 (DVA) 信号，
输出到 AVB 驱动 B 类运动神经元。5-HT 通过 MOD-1 抑制 PVC，实现食物上前进减速。

## 生物学基础

| 连接 | 类型 | 依据 |
|------|------|------|
| PLM → PVC | 化学突触 (GLU, 2 sec) | Chalfie 1985 — 后触加速前进 |
| AIY → PVC | 化学突触 (ACh, 1 sec) | White 1986, Kawano 2011 |
| DVA → PVC | 化学突触 (GLU, 1 sec) | Li 2006, Cook 2019 |
| AVD → PVC | 化学突触 (GLU, 1 sec) | White 1986 — 触觉整合 |
| PVC → AVB | 化学突触 (GLU, 3 sec) | Zheng 1999, Kawano 2011 — 主前进驱动 |
| AVA ↔ PVC | 间隙连接 (2 sec) | White 1986 — 前进/后退互偶 |
| PVC L ↔ R | 间隙连接 (4 sec) | White 1986 — 左右同步 |
| RIS ⊣ PVC | GABA 抑制 (1 sec) | 睡眠抑制前进 |
| 5-HT MOD-1 → PVC | 神经调质 (-5 pA) | Flavell 2013, Zheng 1999 |

### 5-HT 调制机制

```
on-food:  5-HT↑ → MOD-1(Cl⁻) ⊣ PVC → PVC↓ → AVB 弱 → 慢 (dwelling)
off-food: 5-HT↓ → PVC 正常 → AVB 强 → 快 (roaming)
```

MOD-1 是 5-HT 门控 Cl⁻ 通道 (Ranganathan 2000 Nature)，在多个 roaming 促进神经元
(AIY, AIB, AIZ, PVC) 上表达，统一压制前进回路。

## 实现

### 修改文件

| 文件 | 变化 |
|------|------|
| `connectome_builder.cpp` | +2 神经元 (PVCL/PVCR), +8 化学突触, +3 间隙连接, +2 GABA 抑制 |
| `setup_neuromodulation.cpp` | +2 MOD-1 targets (PVC L/R, -5 pA) |
| `simulation_engine.cpp` | "PVC" 加入 prefix 自动注册列表 |

### 神经元总数

132 → **134** (+2: PVCL, PVCR)

## 验证

- **编译**: 零错误
- **regtest**: 17 pass, 0 FAIL (多次运行稳定)

## 参考文献

- Chalfie M et al. (1985) J Neurosci — touch circuit: PLM→PVC→AVB
- White JG et al. (1986) Phil Trans R Soc — C. elegans connectome
- Zheng Y et al. (1999) Neuron — PVC promotes forward, inhibitory interneuron circuit
- Kawano T et al. (2011) Neuron — command interneuron dynamics
- Flavell SW et al. (2013) Cell — MOD-1 inhibits roaming-promoting interneurons
- Ranganathan R et al. (2000) Nature — MOD-1 5-HT-gated Cl⁻ channel
- Cook SJ et al. (2019) Nature — updated C. elegans connectome
