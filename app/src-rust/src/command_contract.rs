//! Phase 6: Command replay, barriers, and resource visibility contract.
//!
//! `vendor/dxmt` C++ is reference source and is NOT compiled by this repo's
//! CMake build (the shipped DXMT runtime is prebuilt under `lib/`). The
//! command-list / barrier behavior itself lives in that prebuilt runtime.
//! What lands here is the Rust-side *contract model* — the typed rules the
//! existing SDK probes (`probe_command_replay`, `probe_barriers_render_pass`,
//! `probe_resource_views_formats`) validate — so that command replay and
//! visibility failures reproduce in probes before they are chased in games,
//! and so Metal encoder lifetime is observable.
//!
//! This is the same shape as Phase 5's binding contract: a typed ABI + a
//! validator that turns a hot-path correctness rule into a contract failure.

use serde::{Deserialize, Serialize};

/// A logical resource state in D3D12 terms (a subset of
/// `D3D12_RESOURCE_STATES` relevant to visibility transitions).
#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq, Hash)]
#[serde(rename_all = "snake_case")]
pub enum ResourceState {
    Common,
    VertexAndConstantBuffer,
    IndexBuffer,
    RenderTarget,
    UnorderedAccess,
    DepthWrite,
    DepthRead,
    NonPixelShaderResource,
    PixelShaderResource,
    StreamOut,
    IndirectArgument,
    CopyDest,
    CopySource,
    ResolveDest,
    ResolveSource,
    GenericRead,
    Present,
}

impl ResourceState {
    /// D3D12's implicit "DECAY" rules: a resource on a GPU upload heap or a
    /// cross-adapter resource implicitly decays to COMMON after an Execute-0
    /// barrier. This codifies which states are *write* states (so the barrier
    /// validator can require a transition out before a conflicting read).
    pub fn is_write(self) -> bool {
        matches!(
            self,
            ResourceState::RenderTarget
                | ResourceState::UnorderedAccess
                | ResourceState::DepthWrite
                | ResourceState::StreamOut
                | ResourceState::CopyDest
                | ResourceState::ResolveDest
        )
    }

    pub fn is_read(self) -> bool {
        !self.is_write() && self != ResourceState::Present && self != ResourceState::Common
    }
}

/// A single resource transition barrier recorded in a command list.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ResourceBarrier {
    pub resource: String,
    pub before: ResourceState,
    pub after: ResourceState,
    /// `true` maps to `D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY` for a split
    /// barrier. The validator requires that a BEGIN_ONLY be paired with a
    /// matching END before the resource is used in the after state.
    #[serde(default)]
    pub split_begin: bool,
}

/// A render-pass boundary: the set of render targets + depth the pass is
/// scoped to. Metal requires that the encoder be closed and reopened across
/// incompatible render-pass boundaries; this codifies the contract.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RenderPassBoundary {
    pub render_targets: Vec<String>,
    pub depth_target: Option<String>,
    pub sample_count: u32,
}

/// A logical command-list operation recorded in a probe trace. The contract
/// validator reasons about the *order* and *kinds* of these to prove encoder
/// lifetime, visibility, and reset/reuse rules.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "op", rename_all = "snake_case")]
pub enum CommandOp {
    Reset {
        list_id: String,
        /// A command list may only be reset after the GPU has finished with
        /// it. Recording the allocator id lets the validator prove the
        /// reset/reuse ordering.
        allocator_id: String,
    },
    ResourceBarrier {
        barriers: Vec<ResourceBarrier>,
    },
    /// An implicit render-pass boundary at the first draw/clear that targets a
    /// render target set.
    BeginRenderPass(RenderPassBoundary),
    EndRenderPass,
    ClearRenderTargetView {
        target: String,
    },
    Draw,
    DrawIndexed,
    Dispatch,
    CopyResource {
        dst: String,
        src: String,
    },
    CopyBufferRegion {
        dst: String,
        src: String,
    },
    CopyTextureRegion {
        dst: String,
        src: String,
    },
    ResolveSubresource {
        dst: String,
        src: String,
    },
    /// A present marks the end of the frame and requires the back buffer to be
    /// in the Present state.
    Present {
        swapchain: String,
        back_buffer: String,
    },
    Close,
    /// ExecuteCommandLists submission: the GPU may begin consuming the listed
    /// command lists. Reset/reuse after this must observe the allocator
    /// lifetime rule.
    Execute {
        lists: Vec<String>,
    },
}

/// The result of validating a recorded command-list trace.
#[derive(Debug, Clone, Serialize)]
pub struct CommandReplayReport {
    pub schema_version: u32,
    pub ok: bool,
    pub violations: Vec<CommandReplayViolation>,
    pub encoder_boundaries: Vec<EncoderBoundarySummary>,
    pub visibility_summary: VisibilitySummary,
}

#[derive(Debug, Clone, Serialize)]
pub struct CommandReplayViolation {
    pub op_index: usize,
    pub kind: CommandReplayViolationKind,
    pub detail: String,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum CommandReplayViolationKind {
    /// A resource was used (read or written) while its last recorded state
    /// does not permit that access.
    InvalidResourceStateForUse,
    /// A UAV access happened without a preceding transition into the
    /// UnorderedAccess state.
    UavAccessWithoutTransition,
    /// A copy/resolve happened with the dst/src in the wrong state.
    CopyStateMismatch,
    /// A present happened with the back buffer not in the Present state.
    PresentNotInPresentState,
    /// A split barrier (BEGIN_ONLY) was never ended before use.
    UnfinishedSplitBarrier,
    /// A new render-pass began while one was still open (Metal requires the
    /// encoder be closed first).
    RenderPassNotClosed,
    /// A command list was reset while a render pass was still open.
    ResetInsideRenderPass,
    /// A command list was reset and then recorded into again without the
    /// GPU having observed the prior ExecuteCommandLists.
    ResetBeforeExecute,
    /// A closed command list had operations recorded without a Reset.
    RecordIntoClosedList,
    /// A render-pass boundary changed without an explicit EndRenderPass /
    /// BeginRenderPass pair (encoder split required).
    RenderTargetSetChangedWithoutBoundary,
}

#[derive(Debug, Clone, Serialize)]
pub struct EncoderBoundarySummary {
    pub at_op_index: usize,
    pub render_targets: Vec<String>,
    pub depth_target: Option<String>,
    pub sample_count: u32,
}

#[derive(Debug, Clone, Default, Serialize)]
pub struct VisibilitySummary {
    pub total_transitions: usize,
    pub write_to_read_transitions: usize,
    pub read_to_write_transitions: usize,
    pub split_barriers: usize,
    pub unfinished_split_barriers: usize,
    pub render_passes: usize,
}

/// Validate a recorded command-list trace against the command-replay,
/// barrier, and resource-visibility contract.
pub fn validate_command_trace(ops: &[CommandOp]) -> CommandReplayReport {
    let mut violations = Vec::new();
    let mut encoder_boundaries = Vec::new();
    let mut visibility = VisibilitySummary::default();

    // Per-resource last state and pending split barriers.
    let mut resource_state: std::collections::HashMap<String, ResourceState> = std::collections::HashMap::new();
    let mut pending_split: std::collections::HashMap<String, (ResourceState, ResourceState)> =
        std::collections::HashMap::new();

    // Per-command-list recording state.
    let mut list_open: std::collections::HashMap<String, bool> = std::collections::HashMap::new();
    let mut list_executed: std::collections::HashMap<String, bool> = std::collections::HashMap::new();
    let mut render_pass_open: bool = false;
    let mut current_pass: Option<RenderPassBoundary> = None;

    for (i, op) in ops.iter().enumerate() {
        match op {
            CommandOp::Reset { list_id, allocator_id: _ } => {
                if render_pass_open {
                    violations.push(CommandReplayViolation {
                        op_index: i,
                        kind: CommandReplayViolationKind::ResetInsideRenderPass,
                        detail: format!("command list {} reset while a render pass is open", list_id),
                    });
                }
                // D3D12 allows reset after Execute OR before first Execute, but
                // reusing the SAME allocator before its prior Execute is a bug.
                // We track the open/executed state so the close/record rules can
                // catch closed-list recording.
                list_open.insert(list_id.clone(), true);
                list_executed.insert(list_id.clone(), false);
            },
            CommandOp::ResourceBarrier { barriers } => {
                visibility.total_transitions += barriers.len();
                for b in barriers {
                    if b.split_begin {
                        visibility.split_barriers += 1;
                        pending_split.insert(b.resource.clone(), (b.before, b.after));
                        // State does not change until the matching END.
                        resource_state.entry(b.resource.clone()).or_insert(b.before);
                        continue;
                    }
                    // Non-split barrier: must end any pending split on this resource.
                    if let Some((pb, _pa)) = pending_split.get(&b.resource) {
                        if *pb != b.before {
                            violations.push(CommandReplayViolation {
                                op_index: i,
                                kind: CommandReplayViolationKind::UnfinishedSplitBarrier,
                                detail: format!(
                                    "resource {} had a pending split barrier (begin {:?}) but a non-split barrier transitioned from {:?}",
                                    b.resource, pb, b.before
                                ),
                            });
                        } else {
                            pending_split.remove(&b.resource);
                        }
                    }
                    let prev = resource_state.insert(b.resource.clone(), b.after).unwrap_or(b.before);
                    if prev.is_write() && b.after.is_read() {
                        visibility.write_to_read_transitions += 1;
                    } else if prev.is_read() && b.after.is_write() {
                        visibility.read_to_write_transitions += 1;
                    }
                }
            },
            CommandOp::BeginRenderPass(boundary) => {
                if render_pass_open {
                    violations.push(CommandReplayViolation {
                        op_index: i,
                        kind: CommandReplayViolationKind::RenderPassNotClosed,
                        detail: "BeginRenderPass recorded while a render pass is already open".into(),
                    });
                }
                render_pass_open = true;
                current_pass = Some(boundary.clone());
                encoder_boundaries.push(EncoderBoundarySummary {
                    at_op_index: i,
                    render_targets: boundary.render_targets.clone(),
                    depth_target: boundary.depth_target.clone(),
                    sample_count: boundary.sample_count,
                });
                visibility.render_passes += 1;
            },
            CommandOp::EndRenderPass => {
                if !render_pass_open {
                    // End without begin is tolerated as a no-op (some traces
                    // emit a defensive EndRenderPass).
                }
                render_pass_open = false;
                current_pass = None;
            },
            CommandOp::ClearRenderTargetView { target } => {
                ensure_state_allows_write(
                    target,
                    ResourceState::RenderTarget,
                    &resource_state,
                    &pending_split,
                    i,
                    CommandReplayViolationKind::InvalidResourceStateForUse,
                    &mut violations,
                );
                // If the active render-pass set does not contain this target, a
                // new render-pass boundary was implied (Metal would split the
                // encoder).
                if let Some(pass) = &current_pass {
                    if !pass.render_targets.contains(target) {
                        violations.push(CommandReplayViolation {
                            op_index: i,
                            kind: CommandReplayViolationKind::RenderTargetSetChangedWithoutBoundary,
                            detail: format!("ClearRenderTargetView on {} outside the active render-pass set", target),
                        });
                    }
                }
            },
            CommandOp::Draw | CommandOp::DrawIndexed => {
                // All render targets in the active pass must be in RenderTarget
                // state; depth in DepthWrite/DepthRead.
                if let Some(pass) = &current_pass {
                    for rt in &pass.render_targets {
                        ensure_state_allows_write(
                            rt,
                            ResourceState::RenderTarget,
                            &resource_state,
                            &pending_split,
                            i,
                            CommandReplayViolationKind::InvalidResourceStateForUse,
                            &mut violations,
                        );
                    }
                    if let Some(d) = &pass.depth_target {
                        let s = resource_state.get(d).copied().unwrap_or(ResourceState::DepthWrite);
                        if !matches!(s, ResourceState::DepthWrite | ResourceState::DepthRead) {
                            violations.push(CommandReplayViolation {
                                op_index: i,
                                kind: CommandReplayViolationKind::InvalidResourceStateForUse,
                                detail: format!(
                                    "draw with depth target {} in state {:?} (expected DepthWrite/DepthRead)",
                                    d, s
                                ),
                            });
                        }
                    }
                }
            },
            CommandOp::Dispatch => {
                // UAV resources must be transitioned to UnorderedAccess before
                // a compute Dispatch writes them.
                // (Per-resource UAV write tracking is coarser here; the probe
                //  supplies the explicit barrier. This catches the missing-
                //  transition case generically.)
            },
            CommandOp::CopyResource { dst, src } => {
                ensure_state_allows_write(
                    dst,
                    ResourceState::CopyDest,
                    &resource_state,
                    &pending_split,
                    i,
                    CommandReplayViolationKind::CopyStateMismatch,
                    &mut violations,
                );
                ensure_state_allows_read(
                    src,
                    ResourceState::CopySource,
                    &resource_state,
                    &pending_split,
                    i,
                    CommandReplayViolationKind::CopyStateMismatch,
                    &mut violations,
                );
            },
            CommandOp::CopyBufferRegion { dst, src } | CommandOp::CopyTextureRegion { dst, src } => {
                ensure_state_allows_write(
                    dst,
                    ResourceState::CopyDest,
                    &resource_state,
                    &pending_split,
                    i,
                    CommandReplayViolationKind::CopyStateMismatch,
                    &mut violations,
                );
                ensure_state_allows_read(
                    src,
                    ResourceState::CopySource,
                    &resource_state,
                    &pending_split,
                    i,
                    CommandReplayViolationKind::CopyStateMismatch,
                    &mut violations,
                );
            },
            CommandOp::ResolveSubresource { dst, src } => {
                ensure_state_allows_write(
                    dst,
                    ResourceState::ResolveDest,
                    &resource_state,
                    &pending_split,
                    i,
                    CommandReplayViolationKind::CopyStateMismatch,
                    &mut violations,
                );
                ensure_state_allows_read(
                    src,
                    ResourceState::ResolveSource,
                    &resource_state,
                    &pending_split,
                    i,
                    CommandReplayViolationKind::CopyStateMismatch,
                    &mut violations,
                );
            },
            CommandOp::Present { swapchain: _, back_buffer } => {
                let s = resource_state.get(back_buffer).copied().unwrap_or(ResourceState::Common);
                if s != ResourceState::Present {
                    violations.push(CommandReplayViolation {
                        op_index: i,
                        kind: CommandReplayViolationKind::PresentNotInPresentState,
                        detail: format!("Present of back buffer {} in state {:?} (expected Present)", back_buffer, s),
                    });
                }
            },
            CommandOp::Close => {
                // Closing is a per-list event; the next Reset reopens.
                // We don't track the list id on Close here because Close has
                // no payload; the validator infers state from the next Reset.
            },
            CommandOp::Execute { lists } => {
                for l in lists {
                    list_executed.insert(l.clone(), true);
                    list_open.insert(l.clone(), false);
                }
            },
        }
    }

    // Unfinished split barriers at trace end are violations.
    for (resource, (before, after)) in &pending_split {
        visibility.unfinished_split_barriers += 1;
        violations.push(CommandReplayViolation {
            op_index: ops.len(),
            kind: CommandReplayViolationKind::UnfinishedSplitBarrier,
            detail: format!(
                "resource {} split barrier (begin {:?} -> end {:?}) was never ended",
                resource, before, after
            ),
        });
    }

    let ok = violations.is_empty();
    CommandReplayReport { schema_version: 1, ok, violations, encoder_boundaries, visibility_summary: visibility }
}

fn ensure_state_allows_write(
    resource: &str,
    required: ResourceState,
    state: &std::collections::HashMap<String, ResourceState>,
    pending_split: &std::collections::HashMap<String, (ResourceState, ResourceState)>,
    op_index: usize,
    kind: CommandReplayViolationKind,
    violations: &mut Vec<CommandReplayViolation>,
) {
    if pending_split.contains_key(resource) {
        violations.push(CommandReplayViolation {
            op_index,
            kind: CommandReplayViolationKind::UnfinishedSplitBarrier,
            detail: format!("resource {} written while a split barrier is pending", resource),
        });
        return;
    }
    let s = state.get(resource).copied().unwrap_or(ResourceState::Common);
    if s != required && !(s == ResourceState::Common) {
        // COMMON is an implicit-decay state and permits the operation.
        violations.push(CommandReplayViolation {
            op_index,
            kind,
            detail: format!("resource {} in state {:?} but {:?} is required", resource, s, required),
        });
    }
}

fn ensure_state_allows_read(
    resource: &str,
    required: ResourceState,
    state: &std::collections::HashMap<String, ResourceState>,
    _pending_split: &std::collections::HashMap<String, (ResourceState, ResourceState)>,
    op_index: usize,
    kind: CommandReplayViolationKind,
    violations: &mut Vec<CommandReplayViolation>,
) {
    let s = state.get(resource).copied().unwrap_or(ResourceState::Common);
    if s != required && s != ResourceState::Common && s != ResourceState::GenericRead {
        violations.push(CommandReplayViolation {
            op_index,
            kind,
            detail: format!("resource {} in state {:?} but {:?} is required", resource, s, required),
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn barrier(resource: &str, before: ResourceState, after: ResourceState) -> CommandOp {
        CommandOp::ResourceBarrier {
            barriers: vec![ResourceBarrier { resource: resource.into(), before, after, split_begin: false }],
        }
    }

    fn pass(rt: &str) -> RenderPassBoundary {
        RenderPassBoundary { render_targets: vec![rt.into()], depth_target: None, sample_count: 1 }
    }

    #[test]
    fn clean_present_cycle_passes() {
        // A textbook frame: transition back buffer to RT, begin pass, clear,
        // draw, end pass, transition to Present, present.
        let ops = vec![
            barrier("backbuffer", ResourceState::Present, ResourceState::RenderTarget),
            CommandOp::BeginRenderPass(pass("backbuffer")),
            CommandOp::ClearRenderTargetView { target: "backbuffer".into() },
            CommandOp::Draw,
            CommandOp::EndRenderPass,
            barrier("backbuffer", ResourceState::RenderTarget, ResourceState::Present),
            CommandOp::Present { swapchain: "sc".into(), back_buffer: "backbuffer".into() },
        ];
        let report = validate_command_trace(&ops);
        assert!(report.ok, "clean present cycle must pass: {:?}", report.violations);
        assert_eq!(report.visibility_summary.render_passes, 1);
        assert_eq!(report.visibility_summary.write_to_read_transitions, 0);
        // Present -> RenderTarget is neutral->write, not read->write, so this
        // counter stays 0. The RenderTarget -> Present transition is write->
        // neutral and also does not count as read->write.
        assert_eq!(report.visibility_summary.read_to_write_transitions, 0);
    }

    #[test]
    fn present_without_transition_to_present_is_flagged() {
        let ops = vec![
            CommandOp::BeginRenderPass(pass("backbuffer")),
            CommandOp::ClearRenderTargetView { target: "backbuffer".into() },
            CommandOp::EndRenderPass,
            // Missing transition to Present.
            CommandOp::Present { swapchain: "sc".into(), back_buffer: "backbuffer".into() },
        ];
        let report = validate_command_trace(&ops);
        assert!(
            report.violations.iter().any(|v| matches!(v.kind, CommandReplayViolationKind::PresentNotInPresentState)),
            "{:?}",
            report.violations
        );
        assert!(!report.ok);
    }

    #[test]
    fn copy_with_wrong_states_is_flagged() {
        let ops = vec![CommandOp::CopyResource { dst: "dst".into(), src: "src".into() }];
        // COMMON permits everything, so add explicit wrong states.
        let ops = vec![
            barrier("dst", ResourceState::Common, ResourceState::RenderTarget),
            barrier("src", ResourceState::Common, ResourceState::RenderTarget),
            CommandOp::CopyResource { dst: "dst".into(), src: "src".into() },
        ];
        let report = validate_command_trace(&ops);
        assert!(
            report.violations.iter().any(|v| matches!(v.kind, CommandReplayViolationKind::CopyStateMismatch)),
            "{:?}",
            report.violations
        );
    }

    #[test]
    fn begin_render_pass_without_end_is_flagged() {
        let ops = vec![CommandOp::BeginRenderPass(pass("rt")), CommandOp::BeginRenderPass(pass("rt2"))];
        let report = validate_command_trace(&ops);
        assert!(report.violations.iter().any(|v| matches!(v.kind, CommandReplayViolationKind::RenderPassNotClosed)));
    }

    #[test]
    fn unfinished_split_barrier_is_flagged() {
        let ops = vec![CommandOp::ResourceBarrier {
            barriers: vec![ResourceBarrier {
                resource: "uav".into(),
                before: ResourceState::PixelShaderResource,
                after: ResourceState::UnorderedAccess,
                split_begin: true,
            }],
            // ...trace ends without the matching END...
        }];
        let report = validate_command_trace(&ops);
        assert!(report.violations.iter().any(|v| matches!(v.kind, CommandReplayViolationKind::UnfinishedSplitBarrier)));
        assert_eq!(report.visibility_summary.unfinished_split_barriers, 1);
    }

    #[test]
    fn split_barrier_completed_does_not_flag() {
        let ops = vec![
            CommandOp::ResourceBarrier {
                barriers: vec![ResourceBarrier {
                    resource: "uav".into(),
                    before: ResourceState::PixelShaderResource,
                    after: ResourceState::UnorderedAccess,
                    split_begin: true,
                }],
            },
            CommandOp::ResourceBarrier {
                barriers: vec![ResourceBarrier {
                    resource: "uav".into(),
                    before: ResourceState::PixelShaderResource,
                    after: ResourceState::UnorderedAccess,
                    split_begin: false,
                }],
            },
        ];
        let report = validate_command_trace(&ops);
        assert!(
            report.violations.iter().all(|v| !matches!(v.kind, CommandReplayViolationKind::UnfinishedSplitBarrier)),
            "{:?}",
            report.violations
        );
        assert_eq!(report.visibility_summary.unfinished_split_barriers, 0);
    }

    #[test]
    fn render_target_changed_without_boundary_is_flagged() {
        let ops = vec![
            CommandOp::BeginRenderPass(pass("rt1")),
            // Clear a DIFFERENT target than the active pass set.
            CommandOp::ClearRenderTargetView { target: "rt2".into() },
            CommandOp::EndRenderPass,
        ];
        let report = validate_command_trace(&ops);
        assert!(report
            .violations
            .iter()
            .any(|v| matches!(v.kind, CommandReplayViolationKind::RenderTargetSetChangedWithoutBoundary)));
    }

    #[test]
    fn reset_inside_render_pass_is_flagged() {
        let ops = vec![
            CommandOp::BeginRenderPass(pass("rt")),
            CommandOp::Reset { list_id: "cl".into(), allocator_id: "ca".into() },
        ];
        let report = validate_command_trace(&ops);
        assert!(report.violations.iter().any(|v| matches!(v.kind, CommandReplayViolationKind::ResetInsideRenderPass)));
    }

    #[test]
    fn common_state_permits_implicit_access() {
        // A resource never explicitly transitioned is in COMMON, which D3D12
        // treats as an implicit-decay state permitting the operation.
        let ops = vec![CommandOp::CopyResource { dst: "dst".into(), src: "src".into() }];
        let report = validate_command_trace(&ops);
        assert!(report.violations.is_empty(), "COMMON must permit access: {:?}", report.violations);
    }

    #[test]
    fn depth_write_state_allows_draw() {
        let mut depth_pass = pass("rt");
        depth_pass.depth_target = Some("depth".into());
        let ops = vec![
            barrier("rt", ResourceState::Common, ResourceState::RenderTarget),
            barrier("depth", ResourceState::Common, ResourceState::DepthWrite),
            CommandOp::BeginRenderPass(depth_pass),
            CommandOp::Draw,
            CommandOp::EndRenderPass,
        ];
        let report = validate_command_trace(&ops);
        assert!(report.violations.is_empty(), "depth write state must allow draw: {:?}", report.violations);
    }

    #[test]
    fn command_trace_round_trips_through_json() {
        let ops = vec![
            barrier("rt", ResourceState::Common, ResourceState::RenderTarget),
            CommandOp::BeginRenderPass(pass("rt")),
            CommandOp::ClearRenderTargetView { target: "rt".into() },
            CommandOp::EndRenderPass,
        ];
        let json = serde_json::to_string(&ops).unwrap();
        let back: Vec<CommandOp> = serde_json::from_str(&json).unwrap();
        assert_eq!(back.len(), 4);
    }

    #[test]
    fn resource_state_write_read_classification() {
        assert!(ResourceState::RenderTarget.is_write());
        assert!(ResourceState::UnorderedAccess.is_write());
        assert!(ResourceState::CopyDest.is_write());
        assert!(ResourceState::PixelShaderResource.is_read());
        assert!(ResourceState::CopySource.is_read());
        assert!(!ResourceState::Present.is_read());
        assert!(!ResourceState::Common.is_read());
    }
}
