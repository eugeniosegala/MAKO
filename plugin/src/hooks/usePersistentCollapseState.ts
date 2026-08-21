import { useEffect, useState } from "react";

/**
 * Keeps one collapsible Decky section in local storage.
 *
 * Reading intentionally fails silently so damaged or unavailable browser
 * storage falls back to the product default. Writes retain the existing
 * warning because a storage failure should not make the controls unusable.
 */
export function usePersistentCollapseState(
  storageKey: string,
  defaultCollapsed: boolean,
  warningLabel: string,
) {
  const [collapsed, setCollapsed] = useState<boolean>(() => {
    try {
      const saved = localStorage.getItem(storageKey);
      return saved !== null ? (JSON.parse(saved) as boolean) : defaultCollapsed;
    } catch {
      return defaultCollapsed;
    }
  });

  useEffect(() => {
    try {
      localStorage.setItem(storageKey, JSON.stringify(collapsed));
    } catch (error) {
      console.warn(`Failed to save ${warningLabel} collapse state:`, error);
    }
  }, [collapsed, storageKey, warningLabel]);

  return [collapsed, setCollapsed] as const;
}
