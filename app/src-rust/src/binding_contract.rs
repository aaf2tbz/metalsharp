//! Phase 5: Descriptor and root-signature Metal binding contract.
//!
//! Treats D3D12 root signatures and descriptor heaps as a formal ABI, not
//! loose per-draw state. This module provides the stable Rust types and
//! validators that mirror what `tools/d3d12-metal-sdk/scripts/dxil-binding-
//! manifest-audit.py` and `dxil-root-signature-audit.py` check on the Metal
//! side, so a binding mismatch becomes a contract failure instead of a
//! game-only mystery.
//!
//! Limits are Metal's documented direct-binding ceilings for argument-buffer
//! fallback layouts (buffers <= 31, textures <= 8, samplers <= 16). These are
//! the same defaults the Python binding audit enforces.

use serde::{Deserialize, Serialize};

/// Metal's direct-binding ceilings for the argument-buffer fallback layout
/// that DXMT/winemetal uses when a stable Metal binding layout is not yet
/// available. These match the Python binding-manifest audit defaults.
pub const MAX_DIRECT_BUFFERS: u32 = 31;
pub const MAX_DIRECT_TEXTURES: u32 = 8;
pub const MAX_DIRECT_SAMPLERS: u32 = 16;

/// D3D12 root signature version. 1.0 is the static root signature; 1.1 adds
/// flags for static/dynamic descriptor behavior.
#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum RootSignatureVersion {
    V1_0,
    V1_1,
}

/// Shader visibility for a root parameter, mirroring
/// `D3D12_SHADER_VISIBILITY`.
#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum ShaderVisibility {
    All,
    Vertex,
    Hull,
    Domain,
    Geometry,
    Pixel,
    Amplification,
    Mesh,
}

/// Descriptor range kind, mirroring `D3D12_DESCRIPTOR_RANGE_TYPE`.
#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum DescriptorRangeKind {
    Srv,
    Uav,
    Cbv,
    Sampler,
}

/// The kind of a root parameter, mirroring `D3D12_ROOT_PARAMETER_TYPE`.
#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum RootParameterKind {
    DescriptorTable,
    Constants,
    Cbv,
    Srv,
    Uav,
}

/// A descriptor range within a descriptor-table root parameter.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DescriptorRange {
    pub kind: DescriptorRangeKind,
    /// Base shader register (`BaseShaderRegister`).
    pub base_register: u32,
    /// Number of descriptors in the range (`NumDescriptors`).
    pub count: u32,
    /// Register space (`RegisterSpace`).
    pub register_space: u32,
    /// Offset in the descriptor table (`OffsetInDescriptorsFromTableStart`).
    pub table_offset: u32,
}

/// A single root parameter. Carries its kind, visibility, and (for descriptor
/// tables) the ranges it spans.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RootParameter {
    pub index: u32,
    pub kind: RootParameterKind,
    pub visibility: ShaderVisibility,
    /// Present only when `kind == DescriptorTable`.
    #[serde(default)]
    pub descriptor_table_ranges: Vec<DescriptorRange>,
    /// Register space used by CBV/SRV/UAV/constants root parameters.
    #[serde(default)]
    pub register_space: u32,
    /// Register index used by CBV/SRV/UAV/constants root parameters.
    #[serde(default)]
    pub register: u32,
}

/// A static sampler recorded in the root signature.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StaticSampler {
    pub index: u32,
    pub register: u32,
    pub register_space: u32,
    pub visibility: ShaderVisibility,
}

/// The null-descriptor policy for a root signature. D3D12 permits null CBV /
/// SRV / UAV descriptors; the binding contract records how they are handled so
/// a null-descriptor regression becomes a visible contract change.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum NullDescriptorPolicy {
    /// Null descriptors are allowed and Metal maps them to no-op bindings.
    Allowed,
    /// Null descriptors are rejected by the runtime.
    Rejected,
    /// Unspecified by this contract.
    Unspecified,
}

/// A root signature ABI manifest. This is the typed form of what DXMT emits
/// and what the root-signature audit parses.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RootSignatureManifest {
    pub schema_version: u32,
    pub version: RootSignatureVersion,
    pub parameters: Vec<RootParameter>,
    pub static_samplers: Vec<StaticSampler>,
    pub null_descriptor_policy: NullDescriptorPolicy,
}

/// A reflection-side binding entry. The shader's declared binding; the
/// reflection-ABI check proves these match the root signature layout.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ReflectionBinding {
    pub shader: String,
    pub kind: DescriptorRangeKind,
    pub register_space: u32,
    pub register: u32,
    pub count: u32,
    pub visibility: ShaderVisibility,
}

/// The result of validating a root signature manifest against Metal's
/// direct-binding limits and D3D12 ABI rules.
#[derive(Debug, Clone, Serialize)]
pub struct BindingContractReport {
    pub schema_version: u32,
    pub ok: bool,
    pub limits: BindingLimits,
    pub violations: Vec<BindingViolation>,
    pub reflection_abi_mismatches: Vec<ReflectionAbiMismatch>,
}

#[derive(Debug, Clone, Copy, Serialize)]
pub struct BindingLimits {
    pub max_direct_buffers: u32,
    pub max_direct_textures: u32,
    pub max_direct_samplers: u32,
}

impl Default for BindingLimits {
    fn default() -> Self {
        BindingLimits {
            max_direct_buffers: MAX_DIRECT_BUFFERS,
            max_direct_textures: MAX_DIRECT_TEXTURES,
            max_direct_samplers: MAX_DIRECT_SAMPLERS,
        }
    }
}

#[derive(Debug, Clone, Serialize)]
pub struct BindingViolation {
    pub root_parameter_index: Option<u32>,
    pub kind: BindingViolationKind,
    pub detail: String,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum BindingViolationKind {
    /// A descriptor-table range exceeds the register bound Metal can address
    /// through the argument-buffer fallback layout.
    DirectBufferLimit,
    DirectTextureLimit,
    DirectSamplerLimit,
    /// A descriptor-table range overlaps an earlier range in the same space.
    OverlappingTableRange,
    /// A static sampler is registered at a register/space that a root
    /// parameter also claims.
    StaticSamplerRegisterClash,
    /// Root parameter indices are not dense from 0.
    SparseRootParameterIndex,
    /// `UNBOUNDED` (`u32::MAX`) count advertised without proven probe support.
    UnboundedRangeUnsupported,
}

#[derive(Debug, Clone, Serialize)]
pub struct ReflectionAbiMismatch {
    pub shader: String,
    pub kind: DescriptorRangeKind,
    pub register_space: u32,
    pub register: u32,
    pub detail: String,
}

/// D3D12 uses `u32::MAX` to express an unbounded descriptor range
/// (`D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND` is also `u32::MAX`, but the count
/// unbounded sentinel is `UINT_MAX` for `NumDescriptors`).
const UNBOUNDED_COUNT: u32 = u32::MAX;

/// Validate a root signature manifest against Metal's direct-binding limits
/// and D3D12 ABI rules. Returns a structured report; `ok == false` means the
/// binding contract is violated.
///
/// This does NOT change runtime behavior. It turns "binding bug" into a
/// contract failure by codifying the limits the runtime must respect.
pub fn validate_root_signature(manifest: &RootSignatureManifest) -> BindingContractReport {
    validate_root_signature_with(manifest, &ReflectionBindingSet::default(), BindingLimits::default())
}

/// Validate a root signature manifest AND prove the reflection bindings match
/// the root signature layout. This is the Phase 5 reflection-ABI check.
pub fn validate_root_signature_with(
    manifest: &RootSignatureManifest,
    reflection: &ReflectionBindingSet,
    limits: BindingLimits,
) -> BindingContractReport {
    let mut violations = Vec::new();
    let mut claimed_ranges: Vec<(u32, u32, DescriptorRangeKind)> = Vec::new();

    for (position, param) in manifest.parameters.iter().enumerate() {
        if param.index != position as u32 {
            violations.push(BindingViolation {
                root_parameter_index: Some(param.index),
                kind: BindingViolationKind::SparseRootParameterIndex,
                detail: format!(
                    "root parameter at position {} has index {}; indices must be dense from 0",
                    position, param.index
                ),
            });
        }
        if param.kind == RootParameterKind::DescriptorTable {
            for range in &param.descriptor_table_ranges {
                if range.count == UNBOUNDED_COUNT {
                    violations.push(BindingViolation {
                        root_parameter_index: Some(param.index),
                        kind: BindingViolationKind::UnboundedRangeUnsupported,
                        detail: format!(
                            "root parameter {} space {} {:?} range is unbounded (count = UINT_MAX); unbounded descriptor ranges require proven probe support before they may be advertised",
                            param.index, range.register_space, range.kind
                        ),
                    });
                    continue;
                }
                let high = range.base_register.saturating_add(range.count);
                let limit = match range.kind {
                    DescriptorRangeKind::Cbv => limits.max_direct_buffers,
                    DescriptorRangeKind::Srv | DescriptorRangeKind::Uav => limits.max_direct_textures,
                    DescriptorRangeKind::Sampler => limits.max_direct_samplers,
                };
                if high > limit {
                    let kind = match range.kind {
                        DescriptorRangeKind::Cbv => BindingViolationKind::DirectBufferLimit,
                        DescriptorRangeKind::Srv | DescriptorRangeKind::Uav => BindingViolationKind::DirectTextureLimit,
                        DescriptorRangeKind::Sampler => BindingViolationKind::DirectSamplerLimit,
                    };
                    violations.push(BindingViolation {
                        root_parameter_index: Some(param.index),
                        kind,
                        detail: format!(
                            "root parameter {} space {} {:?} range covers registers {}..={}; exceeds Metal direct-binding limit {}",
                            param.index, range.register_space, range.kind, range.base_register, high.saturating_sub(1), limit
                        ),
                    });
                }
                // Overlap detection within the same space + kind.
                let claimed_same = claimed_ranges.iter().any(|(space, end, kind)| {
                    *space == range.register_space && *kind == range.kind && *end > range.base_register
                });
                if claimed_same {
                    violations.push(BindingViolation {
                        root_parameter_index: Some(param.index),
                        kind: BindingViolationKind::OverlappingTableRange,
                        detail: format!(
                            "root parameter {} space {} {:?} range overlaps an earlier range",
                            param.index, range.register_space, range.kind
                        ),
                    });
                }
                claimed_ranges.push((range.register_space, high, range.kind));
            }
        }
    }

    // Static sampler register clashes.
    for sampler in &manifest.static_samplers {
        for param in &manifest.parameters {
            if param.register_space == sampler.register_space
                && param.register == sampler.register
                && matches!(
                    param.kind,
                    RootParameterKind::Cbv
                        | RootParameterKind::Srv
                        | RootParameterKind::Uav
                        | RootParameterKind::Constants
                )
            {
                violations.push(BindingViolation {
                    root_parameter_index: Some(param.index),
                    kind: BindingViolationKind::StaticSamplerRegisterClash,
                    detail: format!(
                        "static sampler #{} space {} register {} clashes with root parameter {}",
                        sampler.index, sampler.register_space, sampler.register, param.index
                    ),
                });
            }
        }
    }

    // Reflection ABI: every reflection binding must be covered by some root
    // parameter range (or a static sampler) in the manifest.
    let mut mismatches = Vec::new();
    for binding in &reflection.bindings {
        let covered_by_table = binding_is_covered_by_table(manifest, binding);
        let covered_by_static_sampler = binding_is_static_sampler(manifest, binding);
        if !covered_by_table && !covered_by_static_sampler {
            mismatches.push(ReflectionAbiMismatch {
                shader: binding.shader.clone(),
                kind: binding.kind,
                register_space: binding.register_space,
                register: binding.register,
                detail: format!(
                    "shader {} declares {:?} space {} register {} but no root parameter covers it",
                    binding.shader, binding.kind, binding.register_space, binding.register
                ),
            });
        }
    }

    let ok = violations.is_empty() && mismatches.is_empty();
    BindingContractReport { schema_version: 1, ok, limits, violations, reflection_abi_mismatches: mismatches }
}

fn binding_is_covered_by_table(manifest: &RootSignatureManifest, binding: &ReflectionBinding) -> bool {
    manifest.parameters.iter().any(|p| {
        if p.visibility != ShaderVisibility::All && p.visibility != binding.visibility {
            return false;
        }
        p.descriptor_table_ranges.iter().any(|r| {
            r.kind == binding.kind
                && r.register_space == binding.register_space
                && r.base_register <= binding.register
                && r.count != UNBOUNDED_COUNT
                && r.base_register + r.count > binding.register
        })
    })
}

fn binding_is_static_sampler(manifest: &RootSignatureManifest, binding: &ReflectionBinding) -> bool {
    binding.kind == DescriptorRangeKind::Sampler
        && manifest.static_samplers.iter().any(|s| {
            s.register_space == binding.register_space
                && s.register == binding.register
                && (s.visibility == ShaderVisibility::All || s.visibility == binding.visibility)
        })
}

/// A set of reflection bindings (typically parsed from DXMT's shader
/// reflection output) used for the reflection-ABI check.
#[derive(Debug, Clone, Default)]
pub struct ReflectionBindingSet {
    pub bindings: Vec<ReflectionBinding>,
}

impl ReflectionBindingSet {
    pub fn from_bindings(bindings: Vec<ReflectionBinding>) -> Self {
        ReflectionBindingSet { bindings }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn table_param(index: u32, ranges: Vec<DescriptorRange>) -> RootParameter {
        RootParameter {
            index,
            kind: RootParameterKind::DescriptorTable,
            visibility: ShaderVisibility::All,
            descriptor_table_ranges: ranges,
            register_space: 0,
            register: 0,
        }
    }

    fn range(kind: DescriptorRangeKind, base: u32, count: u32, space: u32, offset: u32) -> DescriptorRange {
        DescriptorRange { kind, base_register: base, count, register_space: space, table_offset: offset }
    }

    fn manifest(params: Vec<RootParameter>) -> RootSignatureManifest {
        RootSignatureManifest {
            schema_version: 1,
            version: RootSignatureVersion::V1_0,
            parameters: params,
            static_samplers: Vec::new(),
            null_descriptor_policy: NullDescriptorPolicy::Allowed,
        }
    }

    #[test]
    fn clean_root_signature_within_limits_passes() {
        let m = manifest(vec![table_param(
            0,
            vec![
                range(DescriptorRangeKind::Cbv, 0, 4, 0, 0),
                range(DescriptorRangeKind::Srv, 0, 8, 0, 4),
                range(DescriptorRangeKind::Sampler, 0, 16, 0, 12),
            ],
        )]);
        let report = validate_root_signature(&m);
        assert!(report.ok, "clean manifest must pass: {:?}", report.violations);
        assert!(report.violations.is_empty());
    }

    #[test]
    fn buffer_range_over_limit_is_flagged() {
        // 32 CBV registers exceeds the buffer limit of 31.
        let m = manifest(vec![table_param(0, vec![range(DescriptorRangeKind::Cbv, 0, 32, 0, 0)])]);
        let report = validate_root_signature(&m);
        assert!(!report.ok);
        assert!(
            report.violations.iter().any(|v| matches!(v.kind, BindingViolationKind::DirectBufferLimit)),
            "{:?}",
            report.violations
        );
    }

    #[test]
    fn texture_range_over_limit_is_flagged() {
        // 9 SRV registers exceeds the texture limit of 8.
        let m = manifest(vec![table_param(0, vec![range(DescriptorRangeKind::Srv, 0, 9, 0, 0)])]);
        let report = validate_root_signature(&m);
        assert!(!report.ok);
        assert!(report.violations.iter().any(|v| matches!(v.kind, BindingViolationKind::DirectTextureLimit)));
    }

    #[test]
    fn sampler_range_over_limit_is_flagged() {
        // 17 samplers exceeds the sampler limit of 16.
        let m = manifest(vec![table_param(0, vec![range(DescriptorRangeKind::Sampler, 0, 17, 0, 0)])]);
        let report = validate_root_signature(&m);
        assert!(!report.ok);
        assert!(report.violations.iter().any(|v| matches!(v.kind, BindingViolationKind::DirectSamplerLimit)));
    }

    #[test]
    fn overlapping_ranges_in_same_space_are_flagged() {
        let m = manifest(vec![table_param(
            0,
            vec![
                range(DescriptorRangeKind::Cbv, 0, 4, 0, 0),
                range(DescriptorRangeKind::Cbv, 2, 4, 0, 4), // overlaps 0..3
            ],
        )]);
        let report = validate_root_signature(&m);
        assert!(!report.ok);
        assert!(report.violations.iter().any(|v| matches!(v.kind, BindingViolationKind::OverlappingTableRange)));
    }

    #[test]
    fn distinct_spaces_do_not_overlap() {
        let m = manifest(vec![table_param(
            0,
            vec![
                range(DescriptorRangeKind::Cbv, 0, 4, 0, 0),
                range(DescriptorRangeKind::Cbv, 0, 4, 1, 4), // space 1, no clash
            ],
        )]);
        let report = validate_root_signature(&m);
        assert!(report.ok, "distinct spaces must not overlap: {:?}", report.violations);
    }

    #[test]
    fn sparse_root_parameter_indices_are_flagged() {
        let mut p = table_param(1, vec![range(DescriptorRangeKind::Cbv, 0, 4, 0, 0)]);
        p.index = 1; // not 0
        let m = manifest(vec![p]);
        let report = validate_root_signature(&m);
        assert!(report.violations.iter().any(|v| matches!(v.kind, BindingViolationKind::SparseRootParameterIndex)));
    }

    #[test]
    fn static_sampler_clashing_with_root_parameter_is_flagged() {
        let mut m = manifest(vec![table_param(0, vec![])]);
        m.parameters.push(RootParameter {
            index: 1,
            kind: RootParameterKind::Cbv,
            visibility: ShaderVisibility::All,
            descriptor_table_ranges: Vec::new(),
            register_space: 0,
            register: 5,
        });
        m.static_samplers.push(StaticSampler {
            index: 0,
            register: 5,
            register_space: 0,
            visibility: ShaderVisibility::All,
        });
        let report = validate_root_signature(&m);
        assert!(report.violations.iter().any(|v| matches!(v.kind, BindingViolationKind::StaticSamplerRegisterClash)));
    }

    #[test]
    fn unbounded_range_is_rejected_without_proven_support() {
        let m = manifest(vec![table_param(0, vec![range(DescriptorRangeKind::Srv, 0, UNBOUNDED_COUNT, 0, 0)])]);
        let report = validate_root_signature(&m);
        assert!(
            report.violations.iter().any(|v| matches!(v.kind, BindingViolationKind::UnboundedRangeUnsupported)),
            "{:?}",
            report.violations
        );
    }

    #[test]
    fn reflection_binding_covered_by_table_range_passes() {
        let m = manifest(vec![table_param(
            0,
            vec![range(DescriptorRangeKind::Cbv, 0, 4, 0, 0), range(DescriptorRangeKind::Srv, 0, 8, 0, 4)],
        )]);
        let reflection = ReflectionBindingSet::from_bindings(vec![
            ReflectionBinding {
                shader: "vs_main".into(),
                kind: DescriptorRangeKind::Cbv,
                register_space: 0,
                register: 0,
                count: 1,
                visibility: ShaderVisibility::Vertex,
            },
            ReflectionBinding {
                shader: "ps_main".into(),
                kind: DescriptorRangeKind::Srv,
                register_space: 0,
                register: 7,
                count: 1,
                visibility: ShaderVisibility::Pixel,
            },
        ]);
        let report = validate_root_signature_with(&m, &reflection, BindingLimits::default());
        assert!(report.reflection_abi_mismatches.is_empty(), "{:?}", report.reflection_abi_mismatches);
        assert!(report.ok);
    }

    #[test]
    fn reflection_binding_not_covered_by_any_parameter_is_flagged() {
        let m = manifest(vec![table_param(0, vec![range(DescriptorRangeKind::Cbv, 0, 2, 0, 0)])]);
        // SRV register 0 is declared by the shader but no root parameter covers it.
        let reflection = ReflectionBindingSet::from_bindings(vec![ReflectionBinding {
            shader: "ps_main".into(),
            kind: DescriptorRangeKind::Srv,
            register_space: 0,
            register: 0,
            count: 1,
            visibility: ShaderVisibility::Pixel,
        }]);
        let report = validate_root_signature_with(&m, &reflection, BindingLimits::default());
        assert_eq!(report.reflection_abi_mismatches.len(), 1);
        assert!(!report.ok);
        assert!(report.reflection_abi_mismatches[0].detail.contains("no root parameter covers it"));
    }

    #[test]
    fn reflection_sampler_covered_by_static_sampler_passes() {
        let mut m = manifest(vec![]);
        m.static_samplers.push(StaticSampler {
            index: 0,
            register: 0,
            register_space: 0,
            visibility: ShaderVisibility::Pixel,
        });
        let reflection = ReflectionBindingSet::from_bindings(vec![ReflectionBinding {
            shader: "ps_main".into(),
            kind: DescriptorRangeKind::Sampler,
            register_space: 0,
            register: 0,
            count: 1,
            visibility: ShaderVisibility::Pixel,
        }]);
        let report = validate_root_signature_with(&m, &reflection, BindingLimits::default());
        assert!(report.reflection_abi_mismatches.is_empty(), "{:?}", report.reflection_abi_mismatches);
    }

    #[test]
    fn root_signature_manifest_round_trips_through_json() {
        let m = manifest(vec![table_param(0, vec![range(DescriptorRangeKind::Srv, 0, 4, 0, 0)])]);
        let json = serde_json::to_string(&m).unwrap();
        let back: RootSignatureManifest = serde_json::from_str(&json).unwrap();
        assert_eq!(back.parameters.len(), 1);
        assert_eq!(back.parameters[0].descriptor_table_ranges[0].count, 4);
    }

    #[test]
    fn metal_direct_binding_limits_match_python_audit_defaults() {
        // Phase 5 contract: the Rust limits MUST match the Python binding
        // audit defaults so the two gates agree on what passes.
        assert_eq!(MAX_DIRECT_BUFFERS, 31);
        assert_eq!(MAX_DIRECT_TEXTURES, 8);
        assert_eq!(MAX_DIRECT_SAMPLERS, 16);
    }
}
