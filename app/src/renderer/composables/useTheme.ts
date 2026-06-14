import { ref, watch } from "vue";

export type ThemeName = "dark" | "light" | "developer";

const themes: ThemeName[] = ["dark", "light", "developer"];

function readSavedTheme(): ThemeName {
  const requested = new URLSearchParams(window.location.search).get("theme");
  if (themes.includes(requested as ThemeName)) return requested as ThemeName;
  const saved = localStorage.getItem("metalsharp-theme");
  return themes.includes(saved as ThemeName) ? (saved as ThemeName) : "dark";
}

const theme = ref<ThemeName>(readSavedTheme());

watch(theme, (val) => {
  document.documentElement.dataset.theme = val;
  document.body.classList.toggle("light", val === "light");
  document.body.classList.toggle("developer", val === "developer");
  localStorage.setItem("metalsharp-theme", val);
});

document.documentElement.dataset.theme = theme.value;
document.body.classList.toggle("light", theme.value === "light");
document.body.classList.toggle("developer", theme.value === "developer");

export function useTheme() {
  function toggle() {
    const currentIndex = themes.indexOf(theme.value);
    theme.value = themes[(currentIndex + 1) % themes.length];
  }

  return { theme, toggle };
}
