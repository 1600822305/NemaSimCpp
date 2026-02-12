# Step 78: 轻触习惯化涌现验证

## 动机

验证最基本的非联想学习行为：重复 tap → 反转概率递减。
Rankin 1990 是 C. elegans 学习研究的基石实验。

Step 60 已实现 tap 基础设施（每 10s 一次 60pA 脉冲到 ALM+PLM），
Step 21 已实现 Tsodyks-Markram STP 模型。但两个 bug 阻止习惯化涌现。

## Bug 修复

### Bug 1: 触觉神经元 I_ext 持久化
```
BUG: set_external_current() 不会被自动重置
     → tap 结束后 ALM 仍持续接收 60pA
     → vesicle pool 永久耗竭到 0.063
     → 所有突触传递永久减弱

FIX: 在 apply_touch_stimulus() 开头重置所有触觉神经元 I_ext=0
     ALM, PLM, AVM, OLQ, FLP, IL1 全部重置
     然后根据当前刺激条件重新设置
```

### Bug 2: STP 参数不适配 tap ISI
```
BUG: tau_recovery = 4000ms → 10s ISI 内 91% 恢复 → 无累积耗竭
     alpha_d = 0.0005 → 每次 tap 耗竭量太小

FIX: tau_recovery = 15000ms (Rankin 1990: 恢复时间 ~30-60s)
     alpha_d = 0.001 (2× 增大耗竭速率)
     结果: 10s ISI 内仅 52% 恢复 → 跨 tap 累积耗竭
```

## 实现细节

### 习惯化因果链（涌现，非直接操控）
```
Tap (60pA, 200ms) → ALM + PLM 同时激活
  ↓
ALM→AVD (gap junction 4 sec) → AVD→AVA (chem 2 sec) → 反转驱动
ALM⊣AVB (inh 3 sec, STP) → 前进抑制 → 反转促进
PLM⊣AVA (inh 3 sec, STP) → 反转抑制 → 平衡作用
  ↓ 重复 tap
STP vesicle pool 耗竭 (1.0 → 0.38 over 30 taps)
  ↓
化学突触传递减弱 → 反转驱动不足 → 反转消失
  = 习惯化涌现！
```

### 多机制涌现
习惯化不是来自单一 STP 机制，而是多因素协同：
1. **STP vesicle depletion**: pool 降低 24% (0.503→0.381)
2. **Gap junction 反馈**: AVD 通过 gj 回传到 ALM → 背景耗竭
3. **回路适应**: 反转后不应期 (2s) 限制高频反转
4. **AVA-AVB 平衡**: 反转需要 AVA 超过 AVB 阈值 (Schmitt trigger 0.35)

### 诊断追踪
新增 Section 31 TAP HABITUATION:
- 每次 tap 的 vesicle pool (ALML⊣AVBL 突触)
- 每次 tap 后 2s 窗口内是否发生反转
- 前 5 次 vs 后 5 次反转率对比
- Pool 变化百分比

## 验证结果

### 4 seed × 300s — 完美一致
| Seed | First 5 taps | Last 5 taps | Pool Δ |
|------|-------------|------------|--------|
| 1 | 5/5 (100%) | 0/5 (0%) | -24% |
| 7 | 5/5 (100%) | 0/5 (0%) | -24% |
| 42 | 5/5 (100%) | 0/5 (0%) | -24% |
| 100 | 5/5 (100%) | 0/5 (0%) | -24% |

### 匹配生物学文献 (Rankin 1990)
- ✅ 初始反转概率高（100%）
- ✅ 反复刺激后反转概率下降（100% → 0%）
- ✅ 30 次 tap 内完成习惯化
- ✅ ISI=10s 产生强习惯化

### Regtest: 20/20 PASS

## 修改文件列表

- `src/simulation/simulation_engine.cpp`
  - apply_touch_stimulus(): 添加触觉神经元 I_ext 重置
  - setup_stp_params(): 触觉 STP tau_recovery 4000→15000, alpha_d 0.0005→0.001
- `src/simulation/simulation_engine.h` — 添加 tap_count()/tap_active() 访问器
- `src/simulation/diag_main.cpp` — 添加 Section 31 TAP HABITUATION 诊断

## 参考文献

- Rankin et al. 1990 J Comp Physiol A — tap 习惯化范式
- Wicks & Rankin 1997 — ISI 决定习惯化速率
- Maricq et al. 1995 Nature — GLR-1 介导机械感觉信号
- Tsodyks & Markram 1997 — 突触短时可塑性模型
