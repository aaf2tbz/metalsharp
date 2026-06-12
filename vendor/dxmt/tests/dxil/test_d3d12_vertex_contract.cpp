#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "../../src/airconv/dxil/msl_lowering.hpp"
#include "../../src/d3d12/d3d12_vertex_input.hpp"

static int g_fail = 0;

static void expect_equal(const char *name, uint32_t actual, uint32_t expected) {
  if (actual == expected) {
    std::printf("  [PASS] %s = %u\n", name, actual);
    return;
  }

  std::printf("  [FAIL] %s: expected %u, got %u\n", name, expected, actual);
  g_fail++;
}

static void expect_true(const char *name, bool actual) {
  if (actual) {
    std::printf("  [PASS] %s\n", name);
    return;
  }

  std::printf("  [FAIL] %s\n", name);
  g_fail++;
}

static void expect_skip(const char *name,
                        const dxmt::D3D12DrawSafetyResult &result,
                        dxmt::D3D12DrawSafetySkipReason expected) {
  if (result.reason == expected) {
    std::printf("  [PASS] %s = %s\n", name,
                dxmt::D3D12DrawSafetySkipReasonName(result.reason));
    return;
  }

  std::printf("  [FAIL] %s: expected %s, got %s\n", name,
              dxmt::D3D12DrawSafetySkipReasonName(expected),
              dxmt::D3D12DrawSafetySkipReasonName(result.reason));
  g_fail++;
}

static const dxmt::D3D12ResolvedIAInputElementMetadata *
find_semantic(const dxmt::D3D12IAInputLayoutMetadata &metadata,
              const char *semantic, uint32_t index) {
  for (const auto &element : metadata.elements) {
    if (element.semantic_index == index &&
        dxmt::D3D12SemanticNameEquals(element.semantic_name, semantic))
      return &element;
  }
  return nullptr;
}

int main() {
  std::printf("=== D3D12 Vertex Contract Test ===\n\n");

  constexpr uint32_t kAppend = std::numeric_limits<uint32_t>::max();
  constexpr uint32_t kInvalid = std::numeric_limits<uint32_t>::max();
  constexpr uint32_t kSlotCap = 29;
  constexpr uint32_t kFloat = 2;
  constexpr uint32_t kFloat2 = 3;
  constexpr uint32_t kFloat3 = 4;
  constexpr uint32_t kFloat4 = 5;
  constexpr uint32_t kFmtFloat = 41;
  constexpr uint32_t kFmtFloat2 = 16;
  constexpr uint32_t kFmtFloat3 = 6;
  constexpr uint32_t kFmtFloat4 = 2;

  constexpr uint32_t sparse_mask = (1u << 0) | (1u << 3) | (1u << 7);
  expect_equal("sparse slot 0 table index",
               dxmt::D3D12CompactVertexTableIndex(sparse_mask, 0), 0);
  expect_equal("sparse slot 3 table index",
               dxmt::D3D12CompactVertexTableIndex(sparse_mask, 3), 1);
  expect_equal("sparse slot 7 table index",
               dxmt::D3D12CompactVertexTableIndex(sparse_mask, 7), 2);
  expect_equal("unused slot has no table index",
               dxmt::D3D12CompactVertexTableIndex(sparse_mask, 2), kInvalid);

  constexpr uint32_t dense_mask = (1u << 0) | (1u << 1) | (1u << 2);
  expect_equal("dense slot 0 table index",
               dxmt::D3D12CompactVertexTableIndex(dense_mask, 0), 0);
  expect_equal("dense slot 1 table index",
               dxmt::D3D12CompactVertexTableIndex(dense_mask, 1), 1);
  expect_equal("dense slot 2 table index",
               dxmt::D3D12CompactVertexTableIndex(dense_mask, 2), 2);

  std::vector<dxmt::D3D12IAInputLayoutElementMetadata> dense_layout = {
      {"POSITION", 0, 0, 0, kFmtFloat3, kFloat3, 12},
      {"NORMAL", 0, 1, 0, kFmtFloat3, kFloat3, 12},
      {"TEXCOORD", 0, 2, 0, kFmtFloat2, kFloat2, 8},
  };
  std::vector<dxmt::D3D12IAInputSignatureElementMetadata> dense_signature = {
      {"POSITION", 0, 0},
      {"NORMAL", 0, 1},
      {"TEXCOORD", 0, 2},
  };
  auto dense = dxmt::D3D12BuildIAInputLayoutMetadata(
      dense_layout, dense_signature, kSlotCap, kAppend);
  expect_equal("dense metadata slot mask", dense.slot_mask, dense_mask);
  expect_equal("dense metadata element count",
               static_cast<uint32_t>(dense.elements.size()), 3);
  expect_equal("dense metadata slot 0 table index",
               dense.elements[0].table_index, 0);
  expect_equal("dense metadata slot 1 table index",
               dense.elements[1].table_index, 1);
  expect_equal("dense metadata slot 2 table index",
               dense.elements[2].table_index, 2);

  std::vector<dxmt::D3D12IAInputLayoutElementMetadata> sparse_layout = {
      {"POSITION", 0, 0, 0, kFmtFloat3, kFloat3, 12},
      {"TEXCOORD", 0, 3, 0, kFmtFloat2, kFloat2, 8,
       dxmt::D3D12VertexInputSlotClass::PerInstance, 2},
      {"TEXCOORD", 1, 3, 8, kFmtFloat2, kFloat2, 8,
       dxmt::D3D12VertexInputSlotClass::PerInstance, 2},
      {"COLOR", 0, 7, 0, kFmtFloat4, kFloat4, 16},
  };
  std::vector<dxmt::D3D12IAInputSignatureElementMetadata> sparse_signature = {
      {"POSITION", 0, 0},
      {"TEXCOORD", 0, 5},
      {"TEXCOORD", 1, 6},
      {"COLOR", 0, 9},
  };
  auto sparse = dxmt::D3D12BuildIAInputLayoutMetadata(
      sparse_layout, sparse_signature, kSlotCap, kAppend);
  expect_equal("sparse metadata slot mask", sparse.slot_mask, sparse_mask);
  expect_equal("sparse slot 0 metadata table index",
               sparse.elements[0].table_index, 0);
  expect_equal("sparse slot 3 first metadata table index",
               sparse.elements[1].table_index, 1);
  expect_equal("sparse slot 3 duplicate metadata table index",
               sparse.elements[2].table_index, 1);
  expect_equal("sparse slot 7 metadata table index",
               sparse.elements[3].table_index, 2);
  expect_equal("sparse shader register alias preserved",
               sparse.elements[1].shader_register, 5);
  expect_equal("sparse duplicate shader register alias preserved",
               sparse.elements[2].shader_register, 6);
  expect_equal("sparse color shader register alias preserved",
               sparse.elements[3].shader_register, 9);
  expect_true("metadata table mode is compact",
              sparse.elements[1].table_indexing_mode ==
                  dxmt::D3D12VertexTableIndexingMode::CompactBySlotMask);
  expect_true("metadata table mode name is stable",
              std::string(dxmt::D3D12VertexTableIndexingModeName(
                  sparse.elements[1].table_indexing_mode)) ==
                  "compact_by_slot_mask");

  dxmt::dxil::MSLLoweringOptions sparse_msl_options = {};
  for (const auto &element : sparse.elements) {
    dxmt::dxil::MSLVertexInputElement input = {};
    input.shader_register = element.shader_register;
    input.table_index = element.table_index;
    input.input_slot = element.input_slot;
    input.aligned_byte_offset = element.aligned_byte_offset;
    input.dxgi_format = element.dxgi_format;
    input.metal_format = element.metal_format;
    input.per_instance = element.input_slot_class ==
                         dxmt::D3D12VertexInputSlotClass::PerInstance;
    input.instance_step_rate = element.instance_step_rate;
    input.table_indexing_mode =
        dxmt::dxil::MSLVertexTableIndexingMode::CompactBySlotMask;
    input.system_value = element.system_value;
    sparse_msl_options.vertex_inputs.push_back(input);
  }
  expect_equal(
      "MSL sparse register 5 table index",
      dxmt::dxil::MSLResolveVertexInputTableIndex(5, sparse_msl_options), 1);
  expect_equal(
      "MSL sparse register 6 duplicate table index",
      dxmt::dxil::MSLResolveVertexInputTableIndex(6, sparse_msl_options), 1);
  expect_equal(
      "MSL sparse register 9 table index",
      dxmt::dxil::MSLResolveVertexInputTableIndex(9, sparse_msl_options), 2);
  expect_true("MSL sparse register 5 emits compact table, offset, format, step",
              dxmt::dxil::MSLVertexPullExpression(5, sparse_msl_options) ==
                  "m12_load_vertex_attr(1, 0, 16, 1, 2, vid, iid, buf16, "
                  "buf1, buf29, buf30)");
  expect_true("MSL sparse register 6 emits duplicate-slot offset",
              dxmt::dxil::MSLVertexPullExpression(6, sparse_msl_options) ==
                  "m12_load_vertex_attr(1, 8, 16, 1, 2, vid, iid, buf16, "
                  "buf1, buf29, buf30)");
  expect_true("MSL sparse register 9 emits compact table and float4 format",
              dxmt::dxil::MSLVertexPullExpression(9, sparse_msl_options) ==
                  "m12_load_vertex_attr(2, 0, 2, 0, 1, vid, iid, buf16, "
                  "buf2, buf29, buf30)");

  dxmt::dxil::MSLLoweringOptions raw_msl_options = {};
  dxmt::dxil::MSLVertexInputElement raw_input = {};
  raw_input.shader_register = 5;
  raw_input.table_index = 1;
  raw_input.input_slot = 3;
  raw_input.aligned_byte_offset = 12;
  raw_input.dxgi_format = kFmtFloat2;
  raw_input.table_indexing_mode =
      dxmt::dxil::MSLVertexTableIndexingMode::RawSlot;
  raw_msl_options.vertex_inputs.push_back(raw_input);
  expect_equal("MSL explicit raw slot mode uses input slot",
               dxmt::dxil::MSLResolveVertexInputTableIndex(5, raw_msl_options),
               3);
  expect_true("MSL explicit raw slot expression carries metadata",
              dxmt::dxil::MSLVertexPullExpression(5, raw_msl_options) ==
                  "m12_load_vertex_attr(3, 12, 16, 0, 1, vid, iid, buf16, "
                  "buf3, buf29, buf30)");

  std::vector<dxmt::D3D12VertexBufferViewMetadata> sparse_views = {
      {0, 0x10000000, 256, 12},
      {3, 0x30000000, 512, 8},
      {7, 0x70000000, 1024, 16},
  };
  auto sparse_rows =
      dxmt::D3D12BuildVertexTableRows(sparse.elements, sparse_views, kSlotCap);
  expect_equal("queue replay sparse table row count",
               static_cast<uint32_t>(sparse_rows.size()), 3);
  if (sparse_rows.size() == 3) {
    expect_equal("queue replay table row 0 index", sparse_rows[0].table_index,
                 0);
    expect_equal("queue replay table row 0 slot", sparse_rows[0].input_slot, 0);
    expect_equal("queue replay table row 1 index", sparse_rows[1].table_index,
                 1);
    expect_equal("queue replay table row 1 slot", sparse_rows[1].input_slot, 3);
    expect_equal("queue replay table row 2 index", sparse_rows[2].table_index,
                 2);
    expect_equal("queue replay table row 2 slot", sparse_rows[2].input_slot, 7);
    expect_equal("queue replay table row 1 stride",
                 sparse_rows[1].stride_in_bytes, 8);
  }

  std::vector<dxmt::D3D12IAInputLayoutElementMetadata> append_layout = {
      {"A", 0, 2, kAppend, kFmtFloat3, kFloat3, 12},
      {"B", 0, 2, kAppend, kFmtFloat, kFloat, 4},
      {"C", 0, 2, kAppend, kFmtFloat, kFloat, 2},
      {"D", 0, 5, kAppend, kFmtFloat, kFloat, 1},
  };
  std::vector<dxmt::D3D12IAInputSignatureElementMetadata> append_signature = {
      {"A", 0, 0},
      {"B", 0, 1},
      {"C", 0, 2},
      {"D", 0, 3},
  };
  auto append = dxmt::D3D12BuildIAInputLayoutMetadata(
      append_layout, append_signature, kSlotCap, kAppend);
  expect_equal("append offset first element",
               append.elements[0].aligned_byte_offset, 0);
  expect_equal("append offset second aligned element",
               append.elements[1].aligned_byte_offset, 12);
  expect_equal("append offset third aligned element",
               append.elements[2].aligned_byte_offset, 16);
  expect_equal("append offset one-byte element",
               append.elements[3].aligned_byte_offset, 0);

  std::vector<dxmt::D3D12IAInputLayoutElementMetadata> filtered_layout = {
      {"POSITION", 0, 0, 0, kFmtFloat3, kFloat3, 12},
      {"TEXCOORD", 0, 3, 0, kFmtFloat2, kFloat2, 8},
      {"COLOR", 0, 7, 0, kFmtFloat4, 0, 0,
       dxmt::D3D12VertexInputSlotClass::PerVertex, 1, false},
      {"TANGENT", 0, 28, 0, kFmtFloat4, kFloat4, 16,
       dxmt::D3D12VertexInputSlotClass::PerInstance, 0},
      {"BINORMAL", 0, 29, 0, kFmtFloat4, kFloat4, 16},
  };
  std::vector<dxmt::D3D12IAInputSignatureElementMetadata> filtered_signature = {
      {"POSITION", 0, 0}, {"NORMAL", 0, 1},   {"COLOR", 0, 2},
      {"TANGENT", 0, 4},  {"BINORMAL", 0, 5}, {"SV_VERTEXID", 0, 6, true},
  };
  auto filtered = dxmt::D3D12BuildIAInputLayoutMetadata(
      filtered_layout, filtered_signature, kSlotCap, kAppend);
  expect_equal("filtered slot mask keeps consumed supported vertex inputs",
               filtered.slot_mask, (1u << 0) | (1u << 28));
  expect_equal("filtered metadata element count",
               static_cast<uint32_t>(filtered.elements.size()), 3);
  expect_true("missing semantic consumed by VS has no vertex fetch",
              find_semantic(filtered, "NORMAL", 0) == nullptr);
  expect_true("unconsumed input layout semantic has no vertex fetch",
              find_semantic(filtered, "TEXCOORD", 0) == nullptr);
  expect_true("unsupported format has no vertex fetch",
              find_semantic(filtered, "COLOR", 0) == nullptr);
  expect_true("slot at Metal-backed cap is skipped",
              find_semantic(filtered, "BINORMAL", 0) == nullptr);
  auto tangent = find_semantic(filtered, "TANGENT", 0);
  expect_true("high slot near cap is retained", tangent != nullptr);
  if (tangent) {
    expect_equal("high slot near cap table index", tangent->table_index, 1);
    expect_equal("per-instance step rate zero preserved",
                 tangent->instance_step_rate, 0);
    expect_true("input slot class preserved",
                tangent->input_slot_class ==
                    dxmt::D3D12VertexInputSlotClass::PerInstance);
  }
  auto system_value = find_semantic(filtered, "SV_VERTEXID", 0);
  expect_true("system value metadata retained", system_value != nullptr);
  if (system_value) {
    expect_true("system value does not generate vertex fetch",
                system_value->system_value);
    expect_equal("system value has no input slot", system_value->input_slot,
                 kInvalid);
    expect_equal("system value has no table index", system_value->table_index,
                 kInvalid);
  }

  std::vector<dxmt::D3D12IAInputLayoutElementMetadata> instance_layout = {
      {"I0", 0, 0, 0, kFmtFloat4, kFloat4, 16,
       dxmt::D3D12VertexInputSlotClass::PerInstance, 0},
      {"I1", 0, 1, 0, kFmtFloat4, kFloat4, 16,
       dxmt::D3D12VertexInputSlotClass::PerInstance, 1},
      {"I5", 0, 2, 0, kFmtFloat4, kFloat4, 16,
       dxmt::D3D12VertexInputSlotClass::PerInstance, 5},
  };
  std::vector<dxmt::D3D12IAInputSignatureElementMetadata> instance_signature = {
      {"I0", 0, 0},
      {"I1", 0, 1},
      {"I5", 0, 2},
  };
  auto instance = dxmt::D3D12BuildIAInputLayoutMetadata(
      instance_layout, instance_signature, kSlotCap, kAppend);
  expect_equal("per-instance step rate 0",
               instance.elements[0].instance_step_rate, 0);
  expect_equal("per-instance step rate 1",
               instance.elements[1].instance_step_rate, 1);
  expect_equal("per-instance step rate >1",
               instance.elements[2].instance_step_rate, 5);

  dxmt::dxil::MSLLoweringOptions instance_msl_options = {};
  for (const auto &element : instance.elements) {
    dxmt::dxil::MSLVertexInputElement input = {};
    input.shader_register = element.shader_register;
    input.table_index = element.table_index;
    input.input_slot = element.input_slot;
    input.dxgi_format = element.dxgi_format;
    input.per_instance = true;
    input.instance_step_rate = element.instance_step_rate;
    input.table_indexing_mode =
        dxmt::dxil::MSLVertexTableIndexingMode::CompactBySlotMask;
    instance_msl_options.vertex_inputs.push_back(input);
  }
  expect_true("MSL per-instance step rate 0 is explicit",
              dxmt::dxil::MSLVertexPullExpression(0, instance_msl_options) ==
                  "m12_load_vertex_attr(0, 0, 2, 1, 0, vid, iid, buf16, "
                  "buf0, buf29, buf30)");
  expect_true("MSL per-instance step rate 1 is explicit",
              dxmt::dxil::MSLVertexPullExpression(1, instance_msl_options) ==
                  "m12_load_vertex_attr(1, 0, 2, 1, 1, vid, iid, buf16, "
                  "buf1, buf29, buf30)");
  expect_true("MSL per-instance step rate >1 is explicit",
              dxmt::dxil::MSLVertexPullExpression(2, instance_msl_options) ==
                  "m12_load_vertex_attr(2, 0, 2, 1, 5, vid, iid, buf16, "
                  "buf2, buf29, buf30)");

  dxmt::D3D12DrawSafetyDesc safety = {};
  safety.pso_present = true;
  safety.pso_compiled = true;
  safety.render_pso_ready = true;
  safety.expect_compact_vertex_table = true;
  safety.element_count = 3;
  safety.instance_count = 2;
  safety.start_element = 0;
  safety.start_instance = 0;
  safety.inputs = sparse.elements;
  for (const auto &view : sparse_views) {
    dxmt::D3D12DrawSafetyVertexBuffer safe_view = {};
    safe_view.input_slot = view.input_slot;
    safe_view.buffer_location = view.buffer_location;
    safe_view.size_in_bytes = view.size_in_bytes;
    safe_view.stride_in_bytes = view.stride_in_bytes;
    safe_view.view_supplied = true;
    safe_view.gpu_address_resolved = true;
    safety.vertex_buffers.push_back(safe_view);
  }
  expect_skip("draw safety valid draw encodes",
              dxmt::D3D12ValidateDrawSafety(safety),
              dxmt::D3D12DrawSafetySkipReason::None);

  auto failed_pso = safety;
  failed_pso.pso_compiled = false;
  expect_skip("draw safety skips failed PSO",
              dxmt::D3D12ValidateDrawSafety(failed_pso),
              dxmt::D3D12DrawSafetySkipReason::PipelineStateCompileFailed);

  auto missing_vb = safety;
  missing_vb.vertex_buffers[1].view_supplied = false;
  missing_vb.vertex_buffers[1].buffer_location = 0;
  expect_skip("draw safety skips missing required VB",
              dxmt::D3D12ValidateDrawSafety(missing_vb),
              dxmt::D3D12DrawSafetySkipReason::MissingVertexBuffer);

  auto unresolved_vb = safety;
  unresolved_vb.vertex_buffers[1].gpu_address_resolved = false;
  expect_skip("draw safety skips unresolved required VB",
              dxmt::D3D12ValidateDrawSafety(unresolved_vb),
              dxmt::D3D12DrawSafetySkipReason::UnresolvedVertexBuffer);

  auto zero_stride = safety;
  zero_stride.vertex_buffers[1].stride_in_bytes = 0;
  expect_skip("draw safety skips zero-stride required VB",
              dxmt::D3D12ValidateDrawSafety(zero_stride),
              dxmt::D3D12DrawSafetySkipReason::ZeroStrideVertexBuffer);

  auto raw_table = safety;
  raw_table.inputs[1].table_indexing_mode =
      dxmt::D3D12VertexTableIndexingMode::RawSlot;
  expect_skip("draw safety skips raw table mismatch",
              dxmt::D3D12ValidateDrawSafety(raw_table),
              dxmt::D3D12DrawSafetySkipReason::UnsupportedVertexTableMode);

  auto index_missing = safety;
  index_missing.index_range.indexed = true;
  expect_skip("draw safety skips missing IB",
              dxmt::D3D12ValidateDrawSafety(index_missing),
              dxmt::D3D12DrawSafetySkipReason::MissingIndexBuffer);

  auto index_unresolved = safety;
  index_unresolved.index_range.indexed = true;
  index_unresolved.index_range.index_buffer_supplied = true;
  index_unresolved.index_range.index_buffer_location = 0x90000000;
  index_unresolved.index_range.index_size = 2;
  expect_skip("draw safety skips unresolved IB",
              dxmt::D3D12ValidateDrawSafety(index_unresolved),
              dxmt::D3D12DrawSafetySkipReason::UnresolvedIndexBuffer);

  auto index_oob = safety;
  index_oob.index_range.indexed = true;
  index_oob.index_range.index_buffer_supplied = true;
  index_oob.index_range.index_buffer_resolved = true;
  index_oob.index_range.index_buffer_location = 0x90000000;
  index_oob.index_range.index_buffer_size = 6;
  index_oob.index_range.index_size = 2;
  index_oob.element_count = 4;
  expect_skip("draw safety skips proven index byte OOB",
              dxmt::D3D12ValidateDrawSafety(index_oob),
              dxmt::D3D12DrawSafetySkipReason::IndexRangeOutOfBounds);

  auto vertex_oob = safety;
  vertex_oob.index_range.indexed = true;
  vertex_oob.index_range.index_buffer_supplied = true;
  vertex_oob.index_range.index_buffer_resolved = true;
  vertex_oob.index_range.index_buffer_location = 0x90000000;
  vertex_oob.index_range.index_buffer_size = 6;
  vertex_oob.index_range.index_size = 2;
  vertex_oob.index_range.has_min_max_index = true;
  vertex_oob.index_range.min_index = 0;
  vertex_oob.index_range.max_index = 64;
  expect_skip("draw safety skips proven vertex range OOB",
              dxmt::D3D12ValidateDrawSafety(vertex_oob),
              dxmt::D3D12DrawSafetySkipReason::VertexRangeOutOfBounds);

  std::printf("\n=== Results: %s ===\n", g_fail ? "FAIL" : "PASS");
  return g_fail ? 1 : 0;
}
