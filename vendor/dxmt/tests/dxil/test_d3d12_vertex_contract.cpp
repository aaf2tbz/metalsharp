#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "../../src/d3d12/d3d12_vertex_input.hpp"

static int g_fail = 0;

static void expect_equal(const char *name, uint32_t actual,
                         uint32_t expected) {
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

struct SyntheticInputElement {
  uint32_t slot = 0;
  uint32_t shader_register = 0;
  uint32_t table_index = 0;
  dxmt::D3D12VertexTableIndexingMode table_indexing_mode =
      dxmt::D3D12VertexTableIndexingMode::CompactBySlotMask;
  uint32_t offset = 0;
  uint32_t size = 16;
  bool per_instance = false;
  uint32_t step_rate = 1;
  bool consumed_by_shader = true;
  bool supported_format = true;
  bool system_value = false;
};

struct SyntheticVertexTableEntry {
  uint32_t source_slot = std::numeric_limits<uint32_t>::max();
};

static uint32_t assign_table_indices(std::vector<SyntheticInputElement> &inputs) {
  uint32_t slot_mask = 0;
  for (const auto &input : inputs) {
    if (!input.consumed_by_shader || !input.supported_format || input.system_value)
      continue;
    slot_mask |= 1u << input.slot;
  }

  for (auto &input : inputs) {
    if (!input.consumed_by_shader || !input.supported_format || input.system_value) {
      input.table_index = std::numeric_limits<uint32_t>::max();
      continue;
    }
    input.table_indexing_mode =
        dxmt::D3D12VertexTableIndexingMode::CompactBySlotMask;
    input.table_index = dxmt::D3D12CompactVertexTableIndex(slot_mask, input.slot);
  }

  return slot_mask;
}

static void populate_alias_table(
    const std::vector<SyntheticInputElement> &inputs,
    SyntheticVertexTableEntry table[16]) {
  for (const auto &input : inputs) {
    if (!input.consumed_by_shader || !input.supported_format || input.system_value)
      continue;
    table[input.table_index].source_slot = input.slot;
    table[input.shader_register].source_slot = input.slot;
  }
}

static void assign_append_offsets(std::vector<SyntheticInputElement> &inputs) {
  uint32_t append_offset[32] = {};
  for (auto &input : inputs) {
    if (!input.consumed_by_shader || !input.supported_format || input.system_value)
      continue;
    input.offset = dxmt::D3D12ResolveAlignedInputOffset(append_offset[input.slot],
                                                        input.size);
    append_offset[input.slot] = input.offset + input.size;
  }
}

int main() {
  std::printf("=== D3D12 Vertex Contract Test ===\n\n");

  constexpr uint32_t slot_mask = (1u << 0) | (1u << 3) | (1u << 7);

  expect_equal("sparse slot 0 table index",
               dxmt::D3D12CompactVertexTableIndex(slot_mask, 0), 0);
  expect_equal("sparse slot 3 table index",
               dxmt::D3D12CompactVertexTableIndex(slot_mask, 3), 1);
  expect_equal("sparse slot 7 table index",
               dxmt::D3D12CompactVertexTableIndex(slot_mask, 7), 2);
  expect_equal("unused slot has no table index",
               dxmt::D3D12CompactVertexTableIndex(slot_mask, 2),
               std::numeric_limits<uint32_t>::max());

  constexpr uint32_t dense_mask = (1u << 0) | (1u << 1) | (1u << 2);
  expect_equal("dense slot 0 table index",
               dxmt::D3D12CompactVertexTableIndex(dense_mask, 0), 0);
  expect_equal("dense slot 1 table index",
               dxmt::D3D12CompactVertexTableIndex(dense_mask, 1), 1);
  expect_equal("dense slot 2 table index",
               dxmt::D3D12CompactVertexTableIndex(dense_mask, 2), 2);

  std::vector<SyntheticInputElement> synthetic = {
      {0, 0, 0, dxmt::D3D12VertexTableIndexingMode::CompactBySlotMask,
       0, 16, false, 1},
      {3, 5, 0, dxmt::D3D12VertexTableIndexingMode::CompactBySlotMask,
       0, 16, true, 2},
      {3, 6, 0, dxmt::D3D12VertexTableIndexingMode::CompactBySlotMask,
       0, 16, true, 2},
      {7, 9, 0, dxmt::D3D12VertexTableIndexingMode::CompactBySlotMask,
       0, 16, false, 1},
  };
  uint32_t synthetic_mask = assign_table_indices(synthetic);
  expect_equal("synthetic sparse slot mask", synthetic_mask, slot_mask);
  expect_equal("synthetic slot 0 table index", synthetic[0].table_index, 0);
  expect_equal("synthetic slot 3 first table index", synthetic[1].table_index, 1);
  expect_equal("synthetic slot 3 duplicate table index", synthetic[2].table_index, 1);
  expect_equal("synthetic slot 7 table index", synthetic[3].table_index, 2);
  expect_equal("synthetic per-instance step preserved",
               synthetic[1].per_instance ? synthetic[1].step_rate : 0, 2);
  expect_true("synthetic table mode is compact",
              synthetic[1].table_indexing_mode ==
                  dxmt::D3D12VertexTableIndexingMode::CompactBySlotMask);
  expect_true("synthetic table mode name is stable",
              std::string(dxmt::D3D12VertexTableIndexingModeName(
                  synthetic[1].table_indexing_mode)) == "compact_by_slot_mask");

  SyntheticVertexTableEntry alias_table[16];
  populate_alias_table(synthetic, alias_table);
  expect_equal("compact table[0] source slot",
               alias_table[0].source_slot, 0);
  expect_equal("compact table[1] source slot",
               alias_table[1].source_slot, 3);
  expect_equal("compact table[2] source slot",
               alias_table[2].source_slot, 7);
  expect_equal("shader register 5 alias source slot",
               alias_table[5].source_slot, 3);
  expect_equal("shader register 6 duplicate alias source slot",
               alias_table[6].source_slot, 3);
  expect_equal("shader register 9 alias source slot",
               alias_table[9].source_slot, 7);

  std::vector<SyntheticInputElement> append = {
      {2, 0, 0, dxmt::D3D12VertexTableIndexingMode::CompactBySlotMask,
       0, 12, false, 1},
      {2, 1, 0, dxmt::D3D12VertexTableIndexingMode::CompactBySlotMask,
       0, 4, false, 1},
      {2, 2, 0, dxmt::D3D12VertexTableIndexingMode::CompactBySlotMask,
       0, 2, false, 1},
      {5, 3, 0, dxmt::D3D12VertexTableIndexingMode::CompactBySlotMask,
       0, 1, false, 1},
  };
  assign_append_offsets(append);
  expect_equal("append offset first element", append[0].offset, 0);
  expect_equal("append offset second aligned element", append[1].offset, 12);
  expect_equal("append offset third aligned element", append[2].offset, 16);
  expect_equal("append offset one-byte element", append[3].offset, 0);

  std::vector<SyntheticInputElement> filtered = {
      {0, 0, 0, dxmt::D3D12VertexTableIndexingMode::CompactBySlotMask,
       0, 16, false, 1, true, true, false},
      {3, 1, 0, dxmt::D3D12VertexTableIndexingMode::CompactBySlotMask,
       0, 16, false, 1, false, true, false},
      {7, 2, 0, dxmt::D3D12VertexTableIndexingMode::CompactBySlotMask,
       0, 16, false, 1, true, false, false},
      {8, 3, 0, dxmt::D3D12VertexTableIndexingMode::CompactBySlotMask,
       0, 16, false, 1, true, true, true},
      {28, 4, 0, dxmt::D3D12VertexTableIndexingMode::CompactBySlotMask,
       0, 16, true, 0, true, true, false},
  };
  uint32_t filtered_mask = assign_table_indices(filtered);
  expect_equal("filtered slot mask keeps consumed supported vertex inputs",
               filtered_mask, (1u << 0) | (1u << 28));
  expect_equal("missing semantic has no table index", filtered[1].table_index,
               std::numeric_limits<uint32_t>::max());
  expect_equal("unsupported format has no table index", filtered[2].table_index,
               std::numeric_limits<uint32_t>::max());
  expect_equal("system value has no table index", filtered[3].table_index,
               std::numeric_limits<uint32_t>::max());
  expect_equal("high slot near cap table index", filtered[4].table_index, 1);
  expect_equal("per-instance step rate zero preserved", filtered[4].step_rate, 0);

  std::printf("\n=== Results: %s ===\n", g_fail ? "FAIL" : "PASS");
  return g_fail ? 1 : 0;
}
