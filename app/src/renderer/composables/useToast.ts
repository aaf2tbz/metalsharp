import { ref } from "vue";

interface ToastMessage {
  id: number;
  text: string;
  type: "success" | "error";
}

const toasts = ref<ToastMessage[]>([]);
let nextId = 0;
const DEFAULT_TOAST_MS = 4000;

export function useToast() {
  function show(text: string, type: "success" | "error" = "success", durationMs?: number) {
    const id = nextId++;
    toasts.value.push({ id, text, type });
    setTimeout(() => {
      toasts.value = toasts.value.filter((t) => t.id !== id);
    }, durationMs ?? DEFAULT_TOAST_MS);
  }

  return { toasts, show };
}
