#include <cstdint>
#include <cstdio>
#include <limits>
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

struct SyntheticInputElement {
  uint32_t slot = 0;
  uint32_t shader_register = 0;
  uint32_t table_index = 0;
  bool per_instance = false;
  uint32_t step_rate = 1;
};

struct SyntheticVertexTableEntry {
  uint32_t source_slot = std::numeric_limits<uint32_t>::max();
};

static uint32_t assign_table_indices(std::vector<SyntheticInputElement> &inputs) {
  uint32_t slot_mask = 0;
  for (const auto &input : inputs)
    slot_mask |= 1u << input.slot;

  for (auto &input : inputs)
    input.table_index = dxmt::D3D12CompactVertexTableIndex(slot_mask, input.slot);

  return slot_mask;
}

static void populate_alias_table(
    const std::vector<SyntheticInputElement> &inputs,
    SyntheticVertexTableEntry table[16]) {
  for (const auto &input : inputs) {
    table[input.table_index].source_slot = input.slot;
    table[input.shader_register].source_slot = input.slot;
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
      {0, 0, 0, false, 1},
      {3, 5, 0, true, 2},
      {3, 6, 0, true, 2},
      {7, 9, 0, false, 1},
  };
  uint32_t synthetic_mask = assign_table_indices(synthetic);
  expect_equal("synthetic sparse slot mask", synthetic_mask, slot_mask);
  expect_equal("synthetic slot 0 table index", synthetic[0].table_index, 0);
  expect_equal("synthetic slot 3 first table index", synthetic[1].table_index, 1);
  expect_equal("synthetic slot 3 duplicate table index", synthetic[2].table_index, 1);
  expect_equal("synthetic slot 7 table index", synthetic[3].table_index, 2);
  expect_equal("synthetic per-instance step preserved",
               synthetic[1].per_instance ? synthetic[1].step_rate : 0, 2);

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

  std::printf("\n=== Results: %s ===\n", g_fail ? "FAIL" : "PASS");
  return g_fail ? 1 : 0;
}
