import { ref, watch } from "vue";

const theme = ref<"dark" | "light">(
  (localStorage.getItem("metalsharp-theme") as "dark" | "light") || "dark",
);

watch(theme, (val) => {
  document.documentElement.dataset.theme = val;
  localStorage.setItem("metalsharp-theme", val);
});

document.documentElement.dataset.theme = theme.value;

export function useTheme() {
  function toggle() {
    theme.value = theme.value === "dark" ? "light" : "dark";
  }

  return { theme, toggle };
}
