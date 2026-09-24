/** Find the nearest vertical scroller that owns a focused settings control. */
export function findFocusScrollContainer(
  element: HTMLElement,
): HTMLElement | null {
  const view = element.ownerDocument.defaultView;
  for (
    let parent = element.parentElement;
    parent;
    parent = parent.parentElement
  ) {
    if (
      parent.scrollHeight > parent.clientHeight &&
      /auto|scroll|overlay/.test(view?.getComputedStyle(parent).overflowY ?? "")
    ) {
      return parent;
    }
  }
  return element.ownerDocument.scrollingElement as HTMLElement | null;
}
