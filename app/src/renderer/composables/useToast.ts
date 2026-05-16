import { ref } from "vue";

interface ToastMessage {
  id: number;
  text: string;
  type: "success" | "error";
}

const toasts = ref<ToastMessage[]>([]);
let nextId = 0;

export function useToast() {
  function show(text: string, type: "success" | "error" = "success") {
    const id = nextId++;
    toasts.value.push({ id, text, type });
    setTimeout(() => {
      toasts.value = toasts.value.filter((t) => t.id !== id);
    }, 4000);
  }

  return { toasts, show };
}
