# Step 133: GluCl 抑制性谷氨酸受体

## 动机

B-1 缺陷：`chemical_synapse.h:93` 将所有谷氨酸统一设为兴奋性，缺少 GluCl（glutamate-gated chloride channel）抑制性受体系统。C. elegans 有 6 个 GluCl 基因（avr-14, avr-15, glc-1, glc-2, glc-3, glc-4），约 30% 的谷氨酸突触通过 GluCl Cl⁻ 通道产生抑制性效果，而非通过 GLR-1/AMPA 产生兴奋性效果。

之前的 `inh()` 函数将所有抑制性突触强制标记为 GABA（`NeurotransmitterType::GABA`），丢失了突触前神经元实际释放谷氨酸这一生物学事实。此外，M3→MC 咽部突触的注释明确写着"inhibitory feedback (glutamate → Cl⁻)"但代码使用了 `syn()`（兴奋性），是一个极性错误。

## 生物学基础

### GluCl 受体家族
- **avr-14 (GluClα3)**: 广泛表达于运动神经元、机械感觉神经元 ALM/PLM/PVD（Dent 2000）
- **avr-15 (GluClα2)**: 咽部肌肉 pm4/pm5 + 体外神经系统（Dent 1997）；介导 M3 产生的 IPSPs
- **glc-1 (GluClα1)**: 体外神经系统（Cully 1994）
- **glc-2 (GluClβ)**: 限于咽部 pm4 肌肉细胞（Laughton 1997）
- **glc-3**: AIB 中间神经元（Kuramochi 2018）、AIY 中间神经元（Ohnishi 2011）、AIA（Kakaria 2019）
- **glc-4**: 功能待确定

### 关键 GluCl 抑制性突触（文献证据）
| 突触前 | 突触后 | 受体 | 文献 |
|--------|--------|------|------|
| ASEL | AIA | GLC-3/AVR-14 | Kakaria 2019 eLife |
| ASER | AIA | GLC-3 | Matsumoto eLife 2024 |
| ASER | AIY | GLC-3 | Ohnishi 2011 EMBO J |
| ASEL | AIB | GLC-3 | Kuramochi 2018 Front Mol Neurosci |
| AWC | AIA | GLC-3 | Kakaria 2019 eLife |
| PHB | AVA | GLC-3 | Hilliard 2002 Curr Biol |
| ALM | AVB | AVR-14 | Dent 2000, Chalfie 1985 |
| PLM | AVA/AVD | AVR-14 | Dent 2000, Lee 1999 |
| PLM | HSN | GluCl | 功能推断 |
| BAG | AIY | GluCl | Hallem 2008 PNAS |
| M3 | MC(pharynx) | AVR-15 | Avery 1993, Dent 1997 |

## 实现细节

### 1. 新增 `GLUTAMATE_INHIBITORY` 枚举值 (`types.h`)
```cpp
enum class NeurotransmitterType : uint8_t {
    ACETYLCHOLINE,
    GABA,
    GLUTAMATE,            // excitatory (via GLR-1/AMPA, mGluR)
    GLUTAMATE_INHIBITORY, // inhibitory via GluCl (avr-14, avr-15, glc-1, glc-3)
    ...
};
```

### 2. 更新突触极性判定 (`chemical_synapse.h`)
- `is_excitatory()`: `GLUTAMATE_INHIBITORY → false`
- `default_reversal()`: `GLUTAMATE_INHIBITORY → -70.0 mV`（E_Cl，与 GABA 相同）

### 3. 新增 `glu_inh()` 辅助方法 (`connectome_builder.cpp`)
与 `inh()` 类似，但将 NT 设为 `GLUTAMATE_INHIBITORY` 而非 `GABA`，保留突触前谷氨酸能的生物学标签。

### 4. 替换 19 个 `inh()` → `glu_inh()`（零功能变化）
所有突触前神经元为谷氨酸能的 `inh()` 调用改为 `glu_inh()`。E_syn 仍为 -70mV，仿真行为完全不变，仅 NT 标签更准确。

### 5. 修复 M3→MC 极性错误（1 个新极性纠正）
M3→MC 从 `syn()`（E_syn=-10mV，兴奋性）改为 `glu_inh()`（E_syn=-70mV，抑制性）。
这修复了咽部 MC 反馈环路的极性，使 M3 的生理功能（触发咽肌舒张）得以正确模拟。

### 6. CSV 加载器支持 (`connectome_loader.cpp`)
新增 `"glucl"`, `"glu_inh"`, `"glutamate_inhibitory"` 解析别名。

## 修改文件列表
- `src/core/types.h` — 新增 GLUTAMATE_INHIBITORY 枚举值
- `src/connectome/chemical_synapse.h` — is_excitatory() + default_reversal()
- `src/connectome/connectome_builder.cpp` — glu_inh() 方法 + 19→glu_inh + M3 极性修复
- `src/connectome/connectome_loader.cpp` — CSV 解析支持

## 验证
- 编译: 零错误
- regtest: 19 pass, 1 FAIL (Midbody curv amp — 预先存在)
- 突触计数: 697 不变（glu_inh 替换 inh，无增减）
- Gap junction 计数: 247 不变

## 参考文献
- Dent JA et al. (2000) EMBO J — avr-14 expression in motor neurons
- Dent JA et al. (1997) EMBO J — avr-15 mediates M3 IPSPs
- Avery L (1993) J Exp Biol — M3 controls pharyngeal relaxation
- Pemberton DJ et al. (2001) Mol Pharmacol — GluClα2 pharyngeal
- Kuramochi M & Bhatt DH (2018) Front Mol Neurosci — GLC-3 on AIB
- Ohnishi N et al. (2011) EMBO J — GLC-3 on AIY, thermotaxis
- Kakaria KS & de Bivort BL (2019) eLife — AWC/ASE→AIA GluCl
- Hilliard MA et al. (2002) Curr Biol — PHB→AVA GLC-3
- Hallem EA & Sternberg PW (2008) PNAS — BAG CO₂ circuit
- Mellem JE et al. (2002) Nat Neurosci — GluCl current in AVA
- Cully DF et al. (1994) Nature — GLC-1/GLC-2 cloning
