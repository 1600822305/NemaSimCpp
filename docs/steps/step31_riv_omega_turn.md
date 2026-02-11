# Step 31: RIV-Driven Omega Turn

## 目标
用 RIV 神经元驱动的涌现机制替换硬编码的 omega turn 概率模型。

## 文献基础
- **Gray 2005 PNAS** — RIV 消融减少 omega 频率；RIV 指定 omega 的腹侧偏置
- **Donnelly 2013 PLOS Biology** — TA 通过 LGC-55 (快/ionotropic) + SER-2 (慢/GPCR) 时序协调逃逸反应；omega 在逆转→前进转换时启动
- **Ouellette 2022 eLife** — RIM tyramine + glutamate 协同稳定逆转；tdc-1 突变体减少 reversal-omega 耦合
- **Neural Sequences 2024** — 转弯是神经序列，RIV 编码头部曲率 (67% 数据集)；tyramine 协调序列动力学

## 关键生物学发现
1. **Omega 时序**: "The omega turn is initiated by a steep ventral bend of the head when the animal REINITIATES FORWARD LOCOMOTION" (Donnelly 2013)
2. **Omega 概率 ∝ 逆转长度**: 短逆转 (<3 body bends) 很少产生 omega；长逆转 (>4 bends) 高概率 omega
3. **双通路时序**: LGC-55 (ms) → 停头+停前进 → SER-2 (秒) → VD GABA↓ → 腹侧过度收缩 → omega 深弯

## 实现

### 电路拓扑
```
AVA ═ RIM(gap) → TA释放 → LGC-55 → RIV(-20pA, 抑制)
                                    → SMD(-25pA, 头部停止)  [Step 30]
                                    → AVB(-10pA, 前进抑制)  [Step 30]

AIB → RIV (1 section, L/R 梯度不对称)
RIV ⊣ RMD dorsal (1 section, 抑制背侧肌肉)
```

### Omega 触发机制: Post-Reversal Pulse
1. **逆转期间**: TA 积累 → LGC-55 抑制 RIV → CCA-1 h gate 去失活
2. **逆转结束**: 记录 [TA] 浓度 → 计算 pulse 幅度 = 60 × [TA] pA
3. **Pulse 注入**: tau=400ms 衰减脉冲注入 RIVL/RIVR，L/R 不对称取决于梯度方向 (±30%)
4. **CCA-1 burst**: pulse 克服 TA tonic (-20×[TA]) → RIV 去极化 → CCA-1 burst → release > 0.5
5. **Omega 执行**: curvature_bias 由 RIV L/R 不对称设定，omega_mode 增大 max_dtheta
6. **终止**: CCA-1 失活 + SLO-1 适应 → burst 结束 → 400ms 最小持续时间后 omega 结束

### 涌现特性 (vs 旧硬编码)
| 特性 | 旧 (硬编码) | 新 (RIV 驱动) |
|------|-----------|-------------|
| 概率 | P = 1 - exp(-dur/1000) 公式 | ∝ [TA] at reversal end |
| 方向 | atan2(gradient) | RIV L/R 不对称 (AIB + 梯度脉冲) |
| 持续时间 | 500 + 1000×P ms (固定) | CCA-1 burst 动力学 + 400ms 最小 |
| 强度 | ±8.0 curvature_bias (固定) | 12.0 × RIV_release (动态) |

### 参数
- `riv_tonic`: 1.0 pA (低于 CCA-1 自发振荡阈值)
- `riv_omega_threshold_`: 0.5 (RIV release rate)
- `omega_curv_gain`: 12.0
- Pulse base: 60 × [TA] pA, tau=400ms
- L/R asymmetry: ±30% from tanh(grad_perp × 50)
- Min omega duration: 400ms

## 文件变更
- `src/connectome/connectome_loader.cpp` — RIVL/RIVR neurons + AIB→RIV + RIV⊣RMD synapses
- `src/simulation/simulation_engine.h` — RIV IDs, omega state variables, apply_riv_omega()
- `src/simulation/simulation_engine.cpp` — post-reversal pulse, apply_riv_omega(), apply_head_tonic() RIV drive
- `src/motor/motor_controller.cpp` — RIV 不映射到 motor controller (via curvature_bias bypass)

## 结果
- regtest: 17 pass, 0 FAIL
- Omega count: 4 (baseline 3, within ±30%)
- SMDVL V swing: 75.4 mV (baseline 65.0)
- Heading rate: 11.0 deg/s (baseline 15.0)
- Speed: 0.3 mm/s
- 60s sim: distance to food 14.1→11.5 mm (approaching food)
