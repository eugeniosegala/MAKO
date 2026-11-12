import React from "react";
import {
  cleanup,
  fireEvent,
  render,
  screen,
  waitFor,
} from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, test, vi } from "vitest";

const shaderPicker = vi.hoisted(() => vi.fn());
const restartLabel = vi.hoisted(() => vi.fn());
vi.mock("@decky/api", () => ({
  openFilePicker: shaderPicker,
  FileSelectionType: { FILE: 0 },
}));

vi.mock("@decky/ui", () => ({
  GamepadButton: {
    DIR_LEFT: 11,
    DIR_RIGHT: 12,
  },
  PanelSectionRow: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  ToggleField: ({
    label,
    checked,
    disabled,
    description,
    onChange,
  }: {
    label: React.ReactNode;
    checked: boolean;
    disabled?: boolean;
    description?: React.ReactNode;
    onChange: (value: boolean) => void;
  }) => (
    <div>
      <button
        data-checked={String(checked)}
        disabled={disabled}
        onClick={() => onChange(!checked)}
      >
        {label}
      </button>
      {description}
    </div>
  ),
  SliderField: ({
    label,
    description,
    value,
    min,
    max,
    step,
    notchCount,
    notchTicksVisible,
    disabled,
    onChange,
  }: {
    label: React.ReactNode;
    description?: React.ReactNode;
    value: number;
    min?: number;
    max?: number;
    step?: number;
    notchCount?: number;
    notchTicksVisible?: boolean;
    disabled?: boolean;
    onChange: (value: number) => void;
  }) => (
    <div>
      {description}
      <button
        data-minimum={min}
        data-maximum={max}
        data-step={step}
        data-notch-count={notchCount ?? "none"}
        data-notch-ticks-visible={String(notchTicksVisible ?? false)}
        disabled={disabled}
        onClick={() => onChange(value === 0 ? 30 : value + 1)}
      >
        {label}
      </button>
    </div>
  ),
  Field: ({
    label,
    description,
    children,
  }: {
    label: React.ReactNode;
    description?: React.ReactNode;
    children?: React.ReactNode;
  }) => (
    <div data-testid={label === "Effects" ? "effects-field" : undefined}>
      <span>{label}</span>
      <span>{description}</span>
      {children}
    </div>
  ),
  Dropdown: ({
    rgOptions,
    selectedOption,
    onChange,
  }: {
    rgOptions: Array<{ data: number | string; label: React.ReactNode }>;
    selectedOption: number | string;
    onChange: (option: {
      data: number | string;
      label: React.ReactNode;
    }) => void;
  }) => (
    <button
      data-testid="cadence-probe-interval-dropdown"
      data-options={JSON.stringify(rgOptions.map((option) => option.data))}
      onClick={() => onChange(typeof selectedOption === "string"
        ? rgOptions[(rgOptions.findIndex((option) => option.data === selectedOption) + 1) % rgOptions.length]
        : rgOptions[0])}
    >
      {typeof selectedOption === "string" ? selectedOption : `${selectedOption}s`}
    </button>
  ),
  TextField: ({
    label,
    description,
  }: {
    label: React.ReactNode;
    description?: React.ReactNode;
  }) => (
    <div>
      <span>{label}</span>
      <span>{description}</span>
    </div>
  ),
  ButtonItem: ({
    children,
    onClick,
    disabled,
  }: {
    children: React.ReactNode;
    onClick: () => void;
    disabled?: boolean;
  }) => (
    <button onClick={onClick} disabled={disabled}>
      {children}
    </button>
  ),
  DialogButton: ({
    children,
    onClick,
    disabled,
    focusable,
    style,
  }: React.ButtonHTMLAttributes<HTMLButtonElement> & {
    focusable?: boolean;
    noFocusRing?: boolean;
  }) => (
    <button
      // Steam suppresses activation but deliberately leaves the DOM button
      // enabled and registered in its navigation tree, even when disabled.
      onClick={disabled ? undefined : onClick}
      disabled={false}
      tabIndex={focusable === false ? -1 : 0}
      data-steam-button="true"
      data-steam-focusable={focusable}
      style={style}
    >
      {children}
    </button>
  ),
  showModal: vi.fn(),
}));
vi.mock("../../src/components/MakoUi", async (importOriginal) => ({
  makoDialogButtonStyle: (
    await importOriginal<typeof import("../../src/components/MakoUi")>()
  ).makoDialogButtonStyle,
  MakoExperimentalSettingLabel: ({
    label,
    badgeLabel,
  }: {
    label: string;
    badgeLabel: string;
  }) => (
    <span
      data-mako-experimental-setting-label="true"
      style={{
        alignItems: "flex-start",
        flexDirection: "column",
        rowGap: "4px",
      }}
    >
      <span>{label}</span>
      <span data-mako-experimental-badge="true">{badgeLabel}</span>
    </span>
  ),
  MakoRestartLabel: ({ label }: { label: string }) => {
    restartLabel(label);
    return label;
  },
  MakoInlineTip: ({
    children,
    tone,
  }: {
    children: React.ReactNode;
    tone?: string;
  }) => <div data-tone={tone}>{children}</div>,
  MakoFocusable: ({
    children,
    onActivate,
    onButtonDown,
    onCancel,
    onGamepadFocus,
    onGamepadBlur,
    "flow-children": _flowChildren,
    ...props
  }: React.HTMLAttributes<HTMLDivElement> & {
    onActivate?: () => void;
    onButtonDown?: (event: CustomEvent<{ button: number }>) => void;
    onCancel?: (event: CustomEvent) => void;
    onGamepadFocus?: (event: CustomEvent) => void;
    onGamepadBlur?: (event: CustomEvent) => void;
    "flow-children"?: string;
  }) => {
    const ref = React.useRef<HTMLDivElement>(null);
    React.useEffect(() => {
      const element = ref.current!;
      const handleButtonDown = (event: Event) =>
        onButtonDown?.(event as CustomEvent<{ button: number }>);
      const handleCancel = (event: Event) => {
        if (!onCancel) return;
        onCancel(event as CustomEvent);
        event.stopPropagation();
      };
      const handleFocus = (event: Event) => onGamepadFocus?.(event as CustomEvent);
      const handleBlur = (event: Event) => onGamepadBlur?.(event as CustomEvent);
      element.addEventListener("vgp_onbuttondown", handleButtonDown);
      element.addEventListener("vgp_oncancel", handleCancel);
      element.addEventListener("vgp_onfocus", handleFocus);
      element.addEventListener("vgp_onblur", handleBlur);
      return () => {
        element.removeEventListener("vgp_onbuttondown", handleButtonDown);
        element.removeEventListener("vgp_oncancel", handleCancel);
        element.removeEventListener("vgp_onfocus", handleFocus);
        element.removeEventListener("vgp_onblur", handleBlur);
      };
    }, [onButtonDown, onCancel, onGamepadFocus, onGamepadBlur]);
    return (
      // Steam uses onActivate as the mouse handler when onClick is absent.
      <div
        ref={ref}
        {...props}
        data-steam-flow={_flowChildren ?? "none"}
        onClick={props.onClick ?? onActivate}
      >
        {children}
      </div>
    );
  },
  MakoSettingRelationship: ({ children }: { children: React.ReactNode }) => (
    <div data-mako-setting-relationship="true">{children}</div>
  ),
  MakoSectionHeader: ({
    children,
    topMargin = "26px",
  }: {
    children: React.ReactNode;
    topMargin?: React.CSSProperties["marginTop"];
  }) => <div data-top-margin={topMargin}>{children}</div>,
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (
    _key: string,
    fallback: string,
    replacements: Record<string, string | number> = {},
  ) =>
    Object.entries(replacements).reduce(
      (text, [name, value]) => text.split(`{${name}}`).join(String(value)),
      fallback,
    ),
}));

import { ConfigurationSection } from "../../src/components/ConfigurationSection";
import { showModal } from "@decky/ui";
import { ShadersConfigurationGroup } from "../../src/components/settings/ShadersConfigurationGroup";
import {
  EXTERNAL_VULKAN_LAYER_MANGOHUD,
  EXTERNAL_VULKAN_LAYER_VKBASALT,
  GAMESCOPE_WSI_COMPATIBILITY,
  MANGOHUD_COUNT_REAL_FRAMES,
  MAILBOX_PRESENTATION_COMPATIBILITY,
  SWAPCHAIN_IMAGE_COUNT_COMPATIBILITY,
  VKBASALT_SHARPENING_DLS,
  getDefaults,
} from "../../src/config/configSchema";

afterEach(cleanup);

describe("Configuration controls", () => {
  beforeEach(() => {
    window.SP_REACT = React;
    localStorage.clear();
    vi.clearAllMocks();
  });

  test("retains section headers and spacing for Tools and Overrides", () => {
    render(
      <ConfigurationSection
        config={getDefaults()}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    expect(
      screen
        .getByText("External Tools")
        .getAttribute("data-top-margin"),
    ).toBe("26px");
    expect(
      screen
        .getByText("Manual Overrides")
        .getAttribute("data-top-margin"),
    ).toBe("26px");
  });

  test("leaves shared performance controls to the primary feature flow", () => {
    render(
      <ConfigurationSection
        config={{
          ...getDefaults(),
          ultra_performance: true,
          flow_scale: 0.9,
          allow_fp16: false,
        }}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    expect(screen.queryByText("Flow Scale (70%)")).toBeNull();
    expect(screen.queryByText("Allow FP16 (Restart)")).toBeNull();
  });

  test.each([
    ["advanced", "Advanced Rendering Settings", "Base FPS Cap (Off)"],
    ["compatibility", "Compatibility Settings", "Gamescope WSI (Restart)"],
  ] as const)(
    "opens the %s tab's controls even with a saved collapsed state",
    (group, title, control) => {
      localStorage.setItem("mako-config-collapsed", "true");
      localStorage.setItem("mako-workarounds-collapsed", "true");
      const onConfigChange = vi.fn(async () => undefined);
      const { container } = render(
        <ConfigurationSection
          groups={[group]}
          collapsible={false}
          config={getDefaults()}
          onConfigChange={onConfigChange}
          onConfigUpdate={vi.fn(async () => undefined)}
        />,
      );
      expect(screen.queryByText(title)).toBeNull();
      expect(screen.getByText(control)).toBeTruthy();
      expect(screen.queryByText("External Tools")).toBeNull();
      expect(screen.queryByText("Manual Overrides")).toBeNull();
      expect(
        container.querySelector('[class$="CollapseButton_Container"]'),
      ).toBeNull();
      if (group === "compatibility") {
        fireEvent.click(screen.getByText(control));
        expect(onConfigChange).toHaveBeenCalledWith(
          GAMESCOPE_WSI_COMPATIBILITY,
          true,
        );
      }
    },
  );

  test("retains collapsible Tools and Overrides below the tabs", () => {
    const { container } = render(
      <ConfigurationSection
        groups={["external-tools", "manual-overrides"]}
        config={getDefaults()}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );
    expect(screen.getByText("External Tools")).toBeTruthy();
    expect(screen.getByText("Manual Overrides")).toBeTruthy();
    expect(screen.queryByText("Advanced Rendering Settings")).toBeNull();
    expect(screen.queryByText("Compatibility Settings")).toBeNull();
    expect(screen.queryByText("Enable MangoHud (Restart)")).toBeNull();
    const toolsToggle = container.querySelector(
      ".MAKO_ExternalToolsCollapseButton_Container button",
    ) as HTMLButtonElement;
    fireEvent.click(toolsToggle);
    expect(screen.getByText("Enable MangoHud (Restart)")).toBeTruthy();
    const overridesToggle = container.querySelector(
      ".MAKO_ManualOverridesCollapseButton_Container button",
    ) as HTMLButtonElement;
    fireEvent.click(overridesToggle);
    expect(screen.getByText("Lossless.dll Path (Restart)")).toBeTruthy();
  });

  test("shows automatic per-game vkBasalt controls and the editable path", () => {
    render(
      <ShadersConfigurationGroup
        config={{
          ...getDefaults(),
          external_vulkan_layer: EXTERNAL_VULKAN_LAYER_VKBASALT,
          vkbasalt_sharpening: VKBASALT_SHARPENING_DLS,
          vkbasalt_sharpness: 0.55,
          vkbasalt_dls_denoise: 0.2,
        }}
        isDefaultProfile={false}
        profileName="final-fantasy"
        vkBasaltConfigPath="/home/deck/.config/mako-render/vkbasalt/abc.conf"
        onConfigChange={vi.fn(async () => undefined)}
      />,
    );

    expect(screen.getByText("Enable shaders (Restart)")).toBeTruthy();
    expect(restartLabel).toHaveBeenCalledWith("Enable shaders (Restart)");
    expect(
      screen.getByText(
        /Enable before launch for bundled sharpening, anti-aliasing, and shaders/,
      ),
    ).toBeTruthy();
    expect(screen.queryByText("Experimental")).toBeNull();
    expect(screen.getByText("Sharpening")).toBeTruthy();
    expect(screen.getByText("Sharpness (55%)")).toBeTruthy();
    expect(screen.getByText("DLS denoise (20%)")).toBeTruthy();
    expect(screen.getByText("Anti-aliasing")).toBeTruthy();
    expect(screen.getByText("Effects")).toBeTruthy();
    expect(
      screen.queryByText(/apply live while vkBasalt is active/i),
    ).toBeNull();
    expect(
      screen
        .getAllByText(/^(Effects|Sharpening)$/)
        .map((element) => element.textContent),
    ).toEqual(["Effects", "Sharpening"]);
    expect(screen.queryByRole("checkbox", { name: "Clarity" })).toBeNull();
    const effectsSummary = screen.getByRole("button", {
      name: "Choose effects (0 selected)",
    });
    fireEvent.focus(effectsSummary);
    expect(effectsSummary.style.outline).not.toContain("transparent");
    fireEvent.blur(effectsSummary);
    fireEvent.click(effectsSummary);
    expect(showModal).not.toHaveBeenCalled();
    const done = screen.getByRole("button", { name: "Done" });
    fireEvent.mouseEnter(done);
    expect(done.style.outline).not.toContain("transparent");
    fireEvent.mouseLeave(done);
    expect(
      screen.getByTestId("mako-effects-selector").style.paddingBottom,
    ).toBe("8px");
    const effectsList = screen.getByTestId("mako-effects-list");
    expect(effectsList.style.overflowY).toBe("");
    expect(screen.getByTestId("mako-effects-page").style.height).toBe("198px");
    expect(screen.getAllByRole("checkbox")).toHaveLength(5);
    expect(screen.getByRole("checkbox", { name: "Clarity" })).toBeTruthy();
    expect(screen.getByRole("checkbox", { name: "Levels Plus" })).toBeTruthy();
    const clarity = screen.getByRole("checkbox", { name: "Clarity" });
    fireEvent.mouseEnter(clarity);
    expect(clarity.style.outline).toContain("2px solid");
    clarity.focus();
    fireEvent(
      clarity,
      new CustomEvent("vgp_onbuttondown", {
        bubbles: true,
        cancelable: true,
        detail: { button: 12 },
      }),
    );
    const deband = screen.getByRole("checkbox", { name: "Deband" });
    expect(deband).toBe(clarity);
    expect(document.activeElement).toBe(deband);
    expect(deband.style.outline).not.toContain("transparent");
    fireEvent(
      deband,
      new CustomEvent("vgp_onbuttondown", {
        bubbles: true,
        cancelable: true,
        detail: { button: 11 },
      }),
    );
    expect(screen.getByRole("checkbox", { name: "Clarity" })).toBe(clarity);
    expect(clarity.style.outline).not.toContain("transparent");
    const pager = screen.getByRole("button", { name: "Effects" });
    pager.focus();
    expect(pager.style.outline).toContain("2px solid");
    fireEvent.keyDown(pager, { key: "ArrowRight" });
    expect(screen.queryByRole("checkbox", { name: "Clarity" })).toBeNull();
    expect(screen.getByRole("checkbox", { name: "Curves" })).toBeTruthy();
    expect(document.activeElement).toBe(pager);
    const technicolor2 = screen.getByRole("checkbox", {
      name: "Technicolor 2",
    });
    technicolor2.focus();
    fireEvent.keyDown(technicolor2, { key: "ArrowRight" });
    const monochrome = screen.getByRole("checkbox", { name: "Monochrome" });
    expect(monochrome).toBe(technicolor2);
    expect(document.activeElement).toBe(monochrome);
    const filmGrain = screen.getByRole("checkbox", { name: "Film Grain" });
    filmGrain.focus();
    fireEvent.keyDown(filmGrain, { key: "ArrowRight" });
    expect(document.activeElement).toBe(
      screen.getByRole("checkbox", { name: "Crop & Stretch" }),
    );
    const clearAll = screen.getByRole("button", { name: "Clear all" });
    fireEvent.focus(clearAll);
    expect(clearAll.style.width).toBe("100%");
    expect(clearAll.style.display).toBe("grid");
    expect(clearAll.style.placeItems).toBe("center");
    expect(clearAll.style.marginTop).toBe("8px");
    expect(clearAll.style.outline).toContain("2px solid");
    expect(
      screen.getByText(
        "Advanced options can be edited in /home/deck/.config/mako-render/vkbasalt/abc.conf. MAKO merges only the controls above and preserves every other setting. Manual advanced changes apply on the next launch. This file belongs to the selected profile and is removed when that profile is deleted.",
      ),
    ).toBeTruthy();
  });

  test("controller focus clears stale hover and ignores a previous effect's late blur", () => {
    const onConfigChange = vi.fn(async () => undefined);
    render(
      <ShadersConfigurationGroup
        config={{
          ...getDefaults(),
          external_vulkan_layer: EXTERNAL_VULKAN_LAYER_VKBASALT,
        }}
        isDefaultProfile
        profileName="mako"
        vkBasaltConfigPath=""
        onConfigChange={onConfigChange}
      />,
    );
    fireEvent.click(screen.getByText("Choose effects (0 selected)"));
    const previous = screen.getByRole("checkbox", { name: "Clarity" });
    const next = screen.getByRole("checkbox", { name: "Levels Plus" });
    fireEvent.mouseEnter(previous);
    fireEvent(previous, new CustomEvent("vgp_onfocus", { bubbles: true }));
    fireEvent(next, new CustomEvent("vgp_onfocus", { bubbles: true }));
    expect(previous.style.outline).toContain("transparent");
    expect(next.style.outline).not.toContain("transparent");
    // Moving content under a parked pointer must not override D-pad focus.
    fireEvent.mouseEnter(previous);
    expect(previous.style.outline).toContain("transparent");
    expect(next.style.outline).not.toContain("transparent");
    fireEvent.mouseMove(previous);
    expect(previous.style.outline).not.toContain("transparent");
    expect(next.style.outline).toContain("transparent");
    fireEvent(next, new CustomEvent("vgp_onfocus", { bubbles: true }));
    fireEvent(previous, new CustomEvent("vgp_onblur", { bubbles: true }));
    fireEvent.blur(previous);
    expect(next.style.outline).not.toContain("transparent");
    fireEvent(next, new CustomEvent("vgp_onblur", { bubbles: true }));
    expect(next.style.outline).toContain("transparent");
    fireEvent.mouseEnter(previous);
    expect(previous.style.outline).not.toContain("transparent");
    expect(onConfigChange).not.toHaveBeenCalled();
  });

  test("mouse arrows move exactly one effects page in either direction", () => {
    const onConfigChange = vi.fn(async () => undefined);
    render(
      <ShadersConfigurationGroup
        config={{
          ...getDefaults(),
          external_vulkan_layer: EXTERNAL_VULKAN_LAYER_VKBASALT,
        }}
        isDefaultProfile
        profileName="mako"
        vkBasaltConfigPath=""
        onConfigChange={onConfigChange}
      />,
    );
    fireEvent.click(screen.getByText("Choose effects (0 selected)"));
    const pager = screen.getByRole("button", { name: "Effects" });
    const pageCount = Number(
      pager.getAttribute("aria-description")?.split("/")[1],
    );
    expect(pageCount).toBeGreaterThan(1);
    const expectPage = (page: number) =>
      expect(pager.getAttribute("aria-description")).toBe(
        `${page} / ${pageCount}`,
      );
    const previous = screen.getByTitle("Previous effects page");
    const next = screen.getByTitle("Next effects page");

    fireEvent.click(previous);
    expectPage(1);
    fireEvent.click(next);
    expectPage(2);
    fireEvent.click(previous);
    expectPage(1);
    for (let page = 2; page <= pageCount; page++) {
      fireEvent.click(next);
      expectPage(page);
    }
    fireEvent.click(next);
    expectPage(pageCount);
    fireEvent.click(previous);
    expectPage(pageCount - 1);
    expect(onConfigChange).not.toHaveBeenCalled();
  });

  test("shows custom definitions, retains missing selections, and uses the file picker", async () => {
    const onConfigChange = vi.fn(async () => undefined);
    const onAddShader = vi.fn(async () => undefined);
    const onRefreshShaders = vi.fn(async () => undefined);
    shaderPicker.mockResolvedValue({
      path: "/tmp/tone.fx",
      realpath: "/tmp/tone.fx",
    });
    render(
      <ShadersConfigurationGroup
        config={{
          ...getDefaults(),
          external_vulkan_layer: EXTERNAL_VULKAN_LAYER_VKBASALT,
          vkbasalt_shader: "custom/Tone_A:custom/Missing",
        }}
        isDefaultProfile={false}
        profileName="game"
        vkBasaltConfigPath="/tmp/game.conf"
        customShaderEffects={[
          { id: "custom/Tone_A", name: "Tone_A", path: "/tmp/a.fx" },
        ]}
        onConfigChange={onConfigChange}
        onAddShader={onAddShader}
        onRefreshShaders={onRefreshShaders}
      />,
    );
    // Steam scrolls Field focus bounds into view. Keep the paged selector's
    // bounds independent of the much taller custom-shader help and actions.
    const effectsField = screen.getByTestId("effects-field");
    expect(
      effectsField.contains(screen.getByTestId("mako-effects-selector")),
    ).toBe(true);
    expect(effectsField.contains(screen.getByText("Add custom shader"))).toBe(
      false,
    );
    expect(effectsField.contains(screen.getByText("Refresh"))).toBe(false);
    fireEvent.click(screen.getByText("Choose effects (2 selected)"));
    expect(
      effectsField.contains(screen.getByRole("button", { name: "Clear all" })),
    ).toBe(true);
    fireEvent.mouseEnter(screen.getAllByRole("checkbox")[0]);
    fireEvent.focus(screen.getAllByRole("checkbox")[1]);
    expect(screen.getByTestId("mako-effects-page").style.height).toBe("198px");
    fireEvent.click(screen.getByRole("button", { name: "Done" }));
    fireEvent.click(screen.getByText("Add custom shader"));
    await waitFor(() =>
      expect(onAddShader).toHaveBeenCalledWith("/tmp/tone.fx"),
    );
    expect(shaderPicker).toHaveBeenCalledWith(
      0,
      "/home",
      true,
      true,
      undefined,
      ["fx"],
      false,
      false,
    );
    shaderPicker.mockRejectedValueOnce("User Canceled");
    fireEvent.click(screen.getByText("Add custom shader"));
    await waitFor(() =>
      expect(
        screen.getByText("Add custom shader").closest("button")?.disabled,
      ).toBe(false),
    );
    expect(screen.queryByText(/User Canceled/)).toBeNull();
    expect(onAddShader).toHaveBeenCalledOnce();
    fireEvent.click(screen.getByText("Refresh"));
    await waitFor(() => expect(onRefreshShaders).toHaveBeenCalledOnce());
    fireEvent.click(screen.getByText("Choose effects (2 selected)"));
    for (let page = 0; page < 4; page++)
      fireEvent.click(screen.getByTestId("mako-effects-next-page"));
    expect(
      screen
        .getByRole("checkbox", {
          name: "2. Custom: Missing (definition missing)",
        })
        .getAttribute("aria-checked"),
    ).toBe("true");
    fireEvent.click(
      screen.getByRole("checkbox", {
        name: "2. Custom: Missing (definition missing)",
      }),
    );
    await waitFor(() =>
      expect(onConfigChange).toHaveBeenCalledWith(
        "vkbasalt_shader",
        "custom/Tone_A",
      ),
    );
    fireEvent.click(screen.getByRole("button", { name: "Clear all" }));
    await waitFor(() =>
      expect(onConfigChange).toHaveBeenCalledWith("vkbasalt_shader", "none"),
    );
  });

  test("excludes unavailable shader actions from Steam focus and restores them after busy work", async () => {
    let finish!: () => void;
    const onRefreshShaders = vi.fn(
      () =>
        new Promise<void>((resolve) => {
          finish = resolve;
        }),
    );
    const onCustomizeShaders = vi.fn(async () => undefined);
    const onAddShader = vi.fn(async () => undefined);
    const onDeleteShaders = vi.fn(async () => undefined);
    const props = {
      config: {
        ...getDefaults(),
        external_vulkan_layer: EXTERNAL_VULKAN_LAYER_VKBASALT,
        vkbasalt_shader: "none",
      },
      isDefaultProfile: false,
      profileName: "game",
      vkBasaltConfigPath: "/tmp/game.conf",
      onConfigChange: vi.fn(async () => undefined),
      onCustomizeShaders,
      onAddShader,
      onRefreshShaders,
      onDeleteShaders,
    };
    const { rerender } = render(<ShadersConfigurationGroup {...props} />);
    const actions = [
      "Customize shaders",
      "Add custom shader",
      "Refresh",
      "Delete selected custom shaders",
    ];
    const expectAvailable = (name: string, available: boolean) => {
      const button = screen.getByRole("button", { name }) as HTMLButtonElement;
      expect(button.disabled).toBe(!available);
      // Disabled actions must leave Steam's navigation tree entirely, and
      // native focus restoration must not select them either.
      expect(button.dataset.steamButton).toBe(available ? "true" : undefined);
      expect(button.style.pointerEvents).toBe(available ? "" : "none");
      if (available && name === "Delete selected custom shaders") {
        expect(button.style.background).toContain("linear-gradient");
        expect(button.style.color).toBe("rgb(255, 240, 245)");
      }
      if (!available) {
        expect(button.style.background).toBe("rgb(48, 54, 61)");
        expect(button.style.color).toBe("rgb(139, 148, 158)");
        expect(button.style.boxShadow).toBe("none");
        expect(button.style.outline).toBe("none");
        expect(button.hasAttribute("tabindex")).toBe(false);
        button.focus();
        expect(document.activeElement).not.toBe(button);
      }
    };
    expectAvailable("Customize shaders", false);
    expectAvailable("Delete selected custom shaders", false);
    expectAvailable("Add custom shader", true);
    expectAvailable("Refresh", true);
    fireEvent.click(screen.getByText("Customize shaders"));
    fireEvent.click(screen.getByText("Delete selected custom shaders"));
    expect(onCustomizeShaders).not.toHaveBeenCalled();
    expect(onDeleteShaders).not.toHaveBeenCalled();

    const selectedConfig = {
      ...props.config,
      vkbasalt_shader: "vibrance:custom/Tone",
    };
    rerender(<ShadersConfigurationGroup {...props} config={selectedConfig} />);
    actions.forEach((name) => expectAvailable(name, true));
    const customize = screen.getByText("Customize shaders");
    customize.focus();
    expect(document.activeElement).toBe(customize);
    fireEvent.click(screen.getByText("Refresh"));
    expect(document.activeElement).not.toBe(customize);
    actions.forEach((name) => {
      expectAvailable(name, false);
      fireEvent.click(screen.getByText(name));
    });
    expect(onRefreshShaders).toHaveBeenCalledOnce();
    expect(onCustomizeShaders).not.toHaveBeenCalled();
    expect(onAddShader).not.toHaveBeenCalled();
    expect(onDeleteShaders).not.toHaveBeenCalled();
    finish();
    await waitFor(() => actions.forEach((name) => expectAvailable(name, true)));
    fireEvent.click(screen.getByText("Customize shaders"));
    expect(onCustomizeShaders).toHaveBeenCalledOnce();
    await waitFor(() => expectAvailable("Customize shaders", true));

    rerender(
      <ShadersConfigurationGroup
        {...props}
        config={selectedConfig}
        onCustomizeShaders={undefined}
      />,
    );
    expectAvailable("Customize shaders", false);
    expectAvailable("Delete selected custom shaders", true);

    rerender(
      <ShadersConfigurationGroup
        {...props}
        config={{ ...props.config, vkbasalt_shader: "vibrance:clarity" }}
      />,
    );
    expectAvailable("Customize shaders", true);
    expectAvailable("Delete selected custom shaders", false);
  });

  test("visits newly enabled Customize after leaving Effects, even when Add was previously focused", async () => {
    function ShaderSettings() {
      const [config, setConfig] = React.useState({
        ...getDefaults(),
        external_vulkan_layer: EXTERNAL_VULKAN_LAYER_VKBASALT,
        vkbasalt_shader: "none",
      });
      return (
        <ShadersConfigurationGroup
          config={config}
          isDefaultProfile={false}
          profileName="game"
          vkBasaltConfigPath="/tmp/game.conf"
          onConfigChange={async (key, value) => {
            setConfig((current) => ({ ...current, [key]: value }));
          }}
          onCustomizeShaders={async () => undefined}
          onAddShader={async () => undefined}
          onRefreshShaders={async () => undefined}
          onDeleteShaders={async () => undefined}
        />
      );
    }
    const { container } = render(<ShaderSettings />);
    // Model Steam's column traversal and remembered child on group entry.
    // Focus stays virtual: moving the DOM activeElement would hide this bug.
    const nodeSelector = "[data-steam-flow], [data-steam-button]";
    const parent = (node: HTMLElement): HTMLElement =>
      node.parentElement!.closest<HTMLElement>("[data-steam-flow]") ?? container;
    const children = (group: HTMLElement) =>
      Array.from(group.querySelectorAll<HTMLElement>(nodeSelector)).filter(
        (node) => parent(node) === group,
      );
    const remembered = new WeakMap<HTMLElement, HTMLElement>();
    let focused: HTMLElement;
    const focus = (node: HTMLElement) => {
      focused = node;
      while (node !== container) {
        const group = parent(node);
        remembered.set(group, node);
        node = group;
      }
    };
    const enter = (node: HTMLElement): HTMLElement | undefined => {
      if (node.tabIndex >= 0) return node;
      const previous = remembered.get(node);
      if (previous?.isConnected) return enter(previous);
      for (const child of children(node)) {
        const target = enter(child);
        if (target) return target;
      }
      return undefined;
    };
    const move = (step: -1 | 1) => {
      let node = focused;
      while (node !== container) {
        const group = parent(node);
        if (group.dataset.steamFlow !== "row") {
          const siblings = children(group);
          for (
            let index = siblings.indexOf(node) + step;
            index >= 0 && index < siblings.length;
            index += step
          ) {
            const target = enter(siblings[index]);
            if (target) {
              focus(target);
              return target;
            }
          }
        }
        node = group;
      }
      return undefined;
    };

    const summary = screen.getByRole("button", {
      name: "Choose effects (0 selected)",
    });
    const add = screen.getByRole("button", { name: "Add custom shader" });
    focus(summary);
    expect(move(1)).toBe(add);
    focus(summary);
    fireEvent.click(summary);
    fireEvent.click(screen.getByRole("checkbox", { name: "HDR Look (SDR)" }));
    fireEvent.click(screen.getByRole("checkbox", { name: "Clarity" }));
    await waitFor(() =>
      expect(
        (
          screen.getByRole("button", {
            name: "Customize shaders",
          }) as HTMLButtonElement
        ).disabled,
      ).toBe(false),
    );
    expect(focused!).toBe(summary);
    fireEvent.click(screen.getByText("Done"));
    const customize = screen.getByRole("button", { name: "Customize shaders" });
    expect(move(1)).toBe(customize);
    expect(move(1)).toBe(add);
    expect(move(-1)).toBe(customize);
    expect(document.activeElement).toBe(document.body);

    focus(summary);
    fireEvent.click(summary);
    fireEvent.click(screen.getByRole("button", { name: "Clear all" }));
    await waitFor(() =>
      expect(
        (
          screen.getByRole("button", {
            name: "Customize shaders",
          }) as HTMLButtonElement
        ).disabled,
      ).toBe(true),
    );
    fireEvent.click(screen.getByText("Done"));
    expect(move(1)).toBe(add);
  });

  test("deletes only selected custom shaders, locks selection while busy, and refreshes", async () => {
    let finish!: () => void;
    const onDeleteShaders = vi.fn(
      () =>
        new Promise<void>((resolve) => {
          finish = resolve;
        }),
    );
    const onConfigChange = vi.fn(async () => undefined);
    const props = {
      config: {
        ...getDefaults(),
        external_vulkan_layer: EXTERNAL_VULKAN_LAYER_VKBASALT,
        vkbasalt_shader: "vibrance:custom/Tone:custom/Missing",
      },
      isDefaultProfile: false,
      profileName: "game",
      vkBasaltConfigPath: "/tmp/game.conf",
      customShaderEffects: [
        { id: "custom/Tone", name: "Tone", path: "/tmp/tone.fx" },
        { id: "custom/Other", name: "Other", path: "/tmp/other.fx" },
      ],
      onConfigChange,
      onDeleteShaders,
    };
    const { rerender } = render(<ShadersConfigurationGroup {...props} />);
    fireEvent.click(
      screen.getByRole("button", { name: "Delete selected custom shaders" }),
    );
    expect(onDeleteShaders).toHaveBeenCalledWith([
      "custom/Tone",
      "custom/Missing",
    ]);
    expect(
      (
        screen.getByRole("button", {
          name: "Delete selected custom shaders",
        }) as HTMLButtonElement
      ).disabled,
    ).toBe(true);
    fireEvent.click(screen.getByText("Choose effects (3 selected)"));
    fireEvent.click(screen.getByRole("button", { name: "Clear all" }));
    expect(onConfigChange).not.toHaveBeenCalled();
    finish();
    await waitFor(() =>
      expect(
        (
          screen.getByRole("button", {
            name: "Delete selected custom shaders",
          }) as HTMLButtonElement
        ).disabled,
      ).toBe(false),
    );
    rerender(
      <ShadersConfigurationGroup
        {...props}
        config={{ ...props.config, vkbasalt_shader: "vibrance" }}
        customShaderEffects={[props.customShaderEffects[1]]}
      />,
    );
    fireEvent.click(screen.getByText("Done"));
    await waitFor(() =>
      expect(screen.getByText("Choose effects (1 selected)")).toBeTruthy(),
    );
    expect(
      (
        screen.getByRole("button", {
          name: "Delete selected custom shaders",
        }) as HTMLButtonElement
      ).disabled,
    ).toBe(true);
    fireEvent.click(screen.getByText("Choose effects (1 selected)"));
    for (let page = 0; page < 4; page++)
      fireEvent.click(screen.getByTestId("mako-effects-next-page"));
    expect(
      screen.getByRole("checkbox", { name: "Custom: Other" }),
    ).toBeTruthy();
    expect(
      screen.queryByRole("checkbox", { name: /Custom: Tone|Custom: Missing/ }),
    ).toBeNull();
  });

  test("adds and removes effects in a per-profile ordered stack", async () => {
    const onConfigChange = vi.fn(async () => undefined);
    render(
      <ShadersConfigurationGroup
        config={{
          ...getDefaults(),
          external_vulkan_layer: EXTERNAL_VULKAN_LAYER_VKBASALT,
          vkbasalt_shader: "hdr_look:vibrance",
        }}
        isDefaultProfile={false}
        profileName="final-fantasy"
        vkBasaltConfigPath="/home/deck/.config/mako-render/vkbasalt/abc.conf"
        onConfigChange={onConfigChange}
      />,
    );

    fireEvent.click(screen.getByText("Choose effects (2 selected)"));
    expect(showModal).not.toHaveBeenCalled();
    expect(
      screen
        .getByRole("checkbox", { name: "1. HDR Look (SDR)" })
        .getAttribute("aria-checked"),
    ).toBe("true");
    fireEvent.click(screen.getByRole("checkbox", { name: "Clarity" }));
    await waitFor(() =>
      expect(onConfigChange).toHaveBeenCalledWith(
        "vkbasalt_shader",
        "hdr_look:vibrance:clarity",
      ),
    );
    fireEvent.click(
      screen.getByRole("checkbox", { name: "1. HDR Look (SDR)" }),
    );
    await waitFor(() =>
      expect(onConfigChange).toHaveBeenCalledWith(
        "vkbasalt_shader",
        "vibrance:clarity",
      ),
    );
    fireEvent.click(screen.getByRole("button", { name: "Clear all" }));
    await waitFor(() =>
      expect(onConfigChange).toHaveBeenCalledWith("vkbasalt_shader", "none"),
    );
    fireEvent.click(screen.getByText("Done"));
    expect(screen.queryByTestId("mako-effects-list")).toBeNull();
  });

  test("closes the effects list when focus leaves the selector", async () => {
    render(
      <>
        <button>Before effects</button>
        <ShadersConfigurationGroup
          config={{
            ...getDefaults(),
            external_vulkan_layer: EXTERNAL_VULKAN_LAYER_VKBASALT,
            vkbasalt_shader: "clarity",
          }}
          isDefaultProfile={false}
          profileName="final-fantasy"
          vkBasaltConfigPath="/home/deck/.config/mako-render/vkbasalt/abc.conf"
          onConfigChange={vi.fn(async () => undefined)}
        />
        <button>After effects</button>
      </>,
    );

    fireEvent.click(screen.getByText("Choose effects (1 selected)"));
    const clarity = screen.getByRole("checkbox", { name: "1. Clarity" });
    clarity.focus();
    expect(screen.getByTestId("mako-effects-list")).toBeTruthy();

    screen.getByRole("button", { name: "After effects" }).focus();
    await waitFor(() =>
      expect(screen.queryByTestId("mako-effects-list")).toBeNull(),
    );
  });

  test("Back collapses effects before returning to Decky's plugin list", () => {
    const onDeckyBack = vi.fn();
    document.addEventListener("vgp_oncancel", onDeckyBack);
    try {
      render(
        <ShadersConfigurationGroup
          config={{
            ...getDefaults(),
            external_vulkan_layer: EXTERNAL_VULKAN_LAYER_VKBASALT,
            vkbasalt_shader: "clarity",
          }}
          isDefaultProfile={false}
          profileName="final-fantasy"
          vkBasaltConfigPath="/home/deck/.config/mako-render/vkbasalt/abc.conf"
          onConfigChange={vi.fn(async () => undefined)}
        />,
      );

      fireEvent.click(screen.getByText("Choose effects (1 selected)"));
      const clarity = screen.getByRole("checkbox", { name: "1. Clarity" });
      clarity.focus();
      expect(
        fireEvent(
          clarity,
          new CustomEvent("vgp_oncancel", {
            bubbles: true,
            cancelable: true,
          }),
        ),
      ).toBe(false);
      expect(onDeckyBack).not.toHaveBeenCalled();
      expect(screen.queryByTestId("mako-effects-list")).toBeNull();

      const summary = screen.getByRole("button", {
        name: "Choose effects (1 selected)",
      });
      expect(document.activeElement).toBe(summary);
      expect(summary.style.outline).not.toContain("transparent");
      expect(
        fireEvent(
          summary,
          new CustomEvent("vgp_oncancel", {
            bubbles: true,
            cancelable: true,
          }),
        ),
      ).toBe(true);
      expect(onDeckyBack).toHaveBeenCalledOnce();
    } finally {
      document.removeEventListener("vgp_oncancel", onDeckyBack);
    }
  });

  test("resets the inline effect editor when the selected profile changes", () => {
    const onConfigChange = vi.fn(async () => undefined);
    const { rerender } = render(
      <ShadersConfigurationGroup
        config={{
          ...getDefaults(),
          external_vulkan_layer: EXTERNAL_VULKAN_LAYER_VKBASALT,
          vkbasalt_shader: "hdr_look:vibrance",
        }}
        isDefaultProfile={false}
        profileName="first"
        vkBasaltConfigPath="/home/deck/.config/mako-render/vkbasalt/first.conf"
        onConfigChange={onConfigChange}
      />,
    );
    fireEvent.click(screen.getByText("Choose effects (2 selected)"));
    expect(screen.getByTestId("mako-effects-list")).toBeTruthy();

    rerender(
      <ShadersConfigurationGroup
        config={{
          ...getDefaults(),
          external_vulkan_layer: EXTERNAL_VULKAN_LAYER_VKBASALT,
          vkbasalt_shader: "clarity",
        }}
        isDefaultProfile={false}
        profileName="second"
        vkBasaltConfigPath="/home/deck/.config/mako-render/vkbasalt/second.conf"
        onConfigChange={onConfigChange}
      />,
    );
    expect(screen.queryByTestId("mako-effects-list")).toBeNull();
    fireEvent.click(screen.getByText("Choose effects (1 selected)"));
    expect(
      screen
        .getByRole("checkbox", { name: "1. Clarity" })
        .getAttribute("aria-checked"),
    ).toBe("true");
  });

  test("keeps the effects list open when the same profile's file path resolves", () => {
    const onConfigChange = vi.fn(async () => undefined);
    const config = {
      ...getDefaults(),
      external_vulkan_layer: EXTERNAL_VULKAN_LAYER_VKBASALT,
      vkbasalt_shader: "vibrance",
    };
    const { rerender } = render(
      <ShadersConfigurationGroup
        config={config}
        isDefaultProfile
        profileName="mako"
        vkBasaltConfigPath=""
        onConfigChange={onConfigChange}
      />,
    );
    fireEvent.click(screen.getByText("Choose effects (1 selected)"));

    rerender(
      <ShadersConfigurationGroup
        config={config}
        isDefaultProfile
        profileName="mako"
        vkBasaltConfigPath="/home/deck/.config/vkBasalt/vkBasalt.conf"
        onConfigChange={onConfigChange}
      />,
    );
    expect(screen.getByTestId("mako-effects-list")).toBeTruthy();
    expect(screen.getByRole("checkbox", { name: "1. Vibrance" })).toBeTruthy();

    rerender(
      <ShadersConfigurationGroup
        config={{ ...config, vkbasalt_shader: "vibrance:clarity" }}
        isDefaultProfile
        profileName="mako"
        vkBasaltConfigPath="/home/deck/.config/vkBasalt/vkBasalt.conf"
        onConfigChange={onConfigChange}
      />,
    );
    expect(screen.getByTestId("mako-effects-list")).toBeTruthy();
    expect(screen.getByRole("checkbox", { name: "2. Clarity" })).toBeTruthy();
  });

  test("shows the editable global file for Default without a mode selector", () => {
    render(
      <ShadersConfigurationGroup
        config={{
          ...getDefaults(),
          external_vulkan_layer: EXTERNAL_VULKAN_LAYER_VKBASALT,
        }}
        isDefaultProfile
        profileName="mako"
        vkBasaltConfigPath="/home/deck/.config/vkBasalt/vkBasalt.conf"
        onConfigChange={vi.fn(async () => undefined)}
      />,
    );

    expect(
      screen.getByText(
        "Advanced options can be edited in /home/deck/.config/vkBasalt/vkBasalt.conf. MAKO merges only the controls above and preserves every other setting. Manual advanced changes apply on the next launch. The Default profile uses this global file.",
      ),
    ).toBeTruthy();
    expect(screen.queryByText("vkBasalt Configuration (Restart)")).toBeNull();
  });

  test("marks process-start controls as Restart without marking live controls", () => {
    const { container } = render(
      <ConfigurationSection
        config={getDefaults()}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    expect(screen.queryByText("Allow FP16 (Restart)")).toBeNull();
    expect(screen.queryByText(/^Flow Scale/)).toBeNull();
    expect(screen.queryByText("Lighter FG Model (Restart)")).toBeNull();
    expect(screen.queryByText(/atomic handoff/)).toBeNull();

    fireEvent.click(
      container.querySelector<HTMLButtonElement>(
        ".MAKO_WorkaroundsCollapseButton_Container button",
      )!,
    );
    expect(screen.getByText("Gamescope WSI (Restart)")).toBeTruthy();
    expect(screen.getByText("Game Swapchain Images (Restart)")).toBeTruthy();
    expect(screen.getByText("Disable Steam Deck Mode (Restart)")).toBeTruthy();
    expect(
      screen.getByText("Enable Zink for OpenGL Games (Restart)"),
    ).toBeTruthy();
    expect(screen.getByText("Enable HDR (Restart)")).toBeTruthy();

    fireEvent.click(
      container.querySelector<HTMLButtonElement>(
        ".MAKO_ManualOverridesCollapseButton_Container button",
      )!,
    );
    expect(screen.getByText("Lossless.dll Path (Restart)")).toBeTruthy();
    expect(screen.getByText("GPU (Restart)")).toBeTruthy();
    expect(
      window.getComputedStyle(
        container.querySelector<HTMLElement>(".MAKO_GpuDescription")!,
      ).paddingBottom,
    ).toBe("10px");
    expect(container.querySelector(".MAKO_MatchedProcessesField")).toBeNull();
  });

  test("explains setting ownership with quiet relationship hints", () => {
    const { container } = render(
      <ConfigurationSection
        config={{
          ...getDefaults(),
          adaptive: true,
          adaptive_auto_base_fps_cap: true,
          base_fps_cap: 35,
          target_fps: 120,
        }}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    expect(
      screen.getByText(
        "Controlled by Steady Base Cap (60 FPS). Your manual value remains saved.",
      ),
    ).toBeTruthy();

    const collapseButton = container.querySelector<HTMLButtonElement>(
      ".MAKO_WorkaroundsCollapseButton_Container button",
    );
    fireEvent.click(collapseButton!);

    expect(
      screen.getByText(
        "Recovery disables Steady Base Cap and Base FPS Cap and resets Real Frame Priority to Automatic. Changing either cap or priority turns Recovery off.",
      ),
    ).toBeTruthy();
    expect(
      container.querySelectorAll('[data-mako-setting-relationship="true"]')
        .length,
    ).toBe(2);
  });

  test("locks Base FPS Cap while the Frame Generation factor is 0x", () => {
    render(
      <ConfigurationSection
        config={{
          ...getDefaults(),
          frame_generation_enabled: false,
          adaptive: true,
          adaptive_auto_base_fps_cap: false,
        }}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    expect(
      (
        screen.getByRole("button", {
          name: "Base FPS Cap (Off)",
        }) as HTMLButtonElement
      ).disabled,
    ).toBe(true);
  });

  test("keeps manual override blocks evenly spaced and avoids a duplicate DLL restart hint", () => {
    const { container } = render(
      <ConfigurationSection
        config={getDefaults()}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    fireEvent.click(
      container.querySelector<HTMLButtonElement>(
        ".MAKO_ManualOverridesCollapseButton_Container button",
      )!,
    );

    expect(
      screen.getByText(
        "Optional full path to Lossless.dll. Leave blank to use MAKO Renderer automatic discovery.",
      ),
    ).toBeTruthy();
    expect(
      screen.queryByText(
        "Optional full path to Lossless.dll. Leave blank to use MAKO Renderer automatic discovery. Restart the game after changing it.",
      ),
    ).toBeNull();
    const styles = Array.from(container.querySelectorAll("style"))
      .map((style) => style.textContent ?? "")
      .join("\n");
    expect(styles).toContain("gap: 12px");
    expect(styles).not.toContain("nth-child");
  });

  test("defaults Enable HDR off, preserves saved choices, and labels only HDR experimental", () => {
    const onConfigChange = vi.fn(async () => undefined);
    const props = {
      onConfigChange,
      onConfigUpdate: vi.fn(async () => undefined),
    };
    const { container, rerender } = render(
      <ConfigurationSection {...props} config={getDefaults()} />,
    );
    fireEvent.click(container.querySelector<HTMLButtonElement>(
      ".MAKO_WorkaroundsCollapseButton_Container button",
    )!);
    expect(screen.queryByText("HDR Quality")).toBeNull();
    const hdr = screen.getByText("Enable HDR (Restart)").closest("button")!;
    expect(hdr.textContent).toContain("Experimental");
    expect(hdr.getAttribute("data-checked")).toBe("false");
    fireEvent.click(hdr);
    expect(onConfigChange).toHaveBeenLastCalledWith("disable_hdr_exposure", false);
    const config = { ...getDefaults(), disable_hdr_exposure: false };
    rerender(<ConfigurationSection {...props} config={config} />);
    const enabledHdr = screen.getByText("Enable HDR (Restart)").closest("button")!;
    expect(enabledHdr.getAttribute("data-checked")).toBe("true");
    fireEvent.click(enabledHdr);
    expect(onConfigChange).toHaveBeenLastCalledWith("disable_hdr_exposure", true);
    const quality = screen.getByText("HDR Quality").parentElement!;
    const dropdown = quality.querySelector("button")!;
    expect(dropdown.textContent).toBe("performance");
    expect(JSON.parse(dropdown.getAttribute("data-options")!)).toEqual([
      "quality", "balanced", "performance",
    ]);
    for (const hdr_quality of ["quality", "balanced", "performance"] as const) {
      fireEvent.click(screen.getByText("HDR Quality").parentElement!.querySelector("button")!);
      expect(onConfigChange).toHaveBeenLastCalledWith("hdr_quality", hdr_quality);
      rerender(<ConfigurationSection {...props} config={{ ...config, hdr_quality }} />);
      expect(screen.getByText("HDR Quality").parentElement!.querySelector("button")!.textContent).toBe(hdr_quality);
    }
    rerender(<ConfigurationSection {...props} config={{ ...config, disable_hdr_exposure: true }} />);
    expect(screen.queryByText("HDR Quality")).toBeNull();
    expect(screen.getByText("Enable HDR (Restart)").closest("button")?.getAttribute("data-checked"))
      .toBe("false");
  });

  test("warns when Gamescope WSI is enabled independently", () => {
    const onConfigChange = vi.fn(async () => undefined);
    const { container } = render(
      <ConfigurationSection
        config={getDefaults()}
        onConfigChange={onConfigChange}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    const collapseButton = container.querySelector<HTMLButtonElement>(
      ".MAKO_WorkaroundsCollapseButton_Container button",
    );
    fireEvent.click(collapseButton!);

    expect(screen.getByText("Enable HDR (Restart)")).toBeTruthy();
    fireEvent.click(screen.getByText("Enable HDR (Restart)"));
    expect(onConfigChange).toHaveBeenCalledWith("disable_hdr_exposure", false);

    expect(
      screen
        .getByText(
          "Only supported 64-bit host launches. Leave off unless needed; it may reduce performance.",
        )
        .getAttribute("data-tone"),
    ).toBe("warning");
    fireEvent.click(screen.getByText("Gamescope WSI (Restart)"));
    expect(onConfigChange).toHaveBeenCalledWith(
      GAMESCOPE_WSI_COMPATIBILITY,
      true,
    );
  });

  test("exposes the explicit per-profile swapchain image compatibility policy", () => {
    const onConfigChange = vi.fn(async () => undefined);
    const { container } = render(
      <ConfigurationSection
        config={getDefaults()}
        onConfigChange={onConfigChange}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    fireEvent.click(
      container.querySelector<HTMLButtonElement>(
        ".MAKO_WorkaroundsCollapseButton_Container button",
      )!,
    );

    expect(
      screen
        .getByText(
          "Generated frames may be skipped when the compositor has no spare image, which can reduce smoothness or performance under pressure.",
        )
        .getAttribute("data-tone"),
    ).toBe("warning");
    fireEvent.click(screen.getByText("Game Swapchain Images (Restart)"));
    expect(onConfigChange).toHaveBeenCalledWith(
      SWAPCHAIN_IMAGE_COUNT_COMPATIBILITY,
      true,
    );
  });

  test("exposes the explicit per-profile Mailbox presentation compatibility policy", () => {
    const onConfigChange = vi.fn(async () => undefined);
    const { container } = render(
      <ConfigurationSection
        config={getDefaults()}
        onConfigChange={onConfigChange}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    fireEvent.click(
      container.querySelector<HTMLButtonElement>(
        ".MAKO_WorkaroundsCollapseButton_Container button",
      )!,
    );

    const mailbox = screen.getByText(
      "Mailbox Presentation (Restart)",
    ) as HTMLButtonElement;
    expect(mailbox.getAttribute("data-checked")).toBe("false");
    expect(
      screen
        .getByText(
          "A newer frame can replace a generated frame before it is shown, which can reduce smoothness or make pacing uneven.",
        )
        .getAttribute("data-tone"),
    ).toBe("warning");
    fireEvent.click(mailbox);
    expect(onConfigChange).toHaveBeenCalledExactlyOnceWith(
      MAILBOX_PRESENTATION_COMPATIBILITY,
      true,
    );
  });

  test.each([false, true])(
    "Scaling keeps WSI independently editable when saved as %s",
    (wsiEnabled) => {
      const onConfigChange = vi.fn(async () => undefined);
      const { container } = render(
        <ConfigurationSection
          config={{
            ...getDefaults(),
            scaling_enabled: true,
            gamescope_wsi_compatibility: wsiEnabled,
          }}
          onConfigChange={onConfigChange}
          onConfigUpdate={vi.fn(async () => undefined)}
        />,
      );

      fireEvent.click(
        container.querySelector<HTMLButtonElement>(
          ".MAKO_WorkaroundsCollapseButton_Container button",
        )!,
      );
      const compatibility = screen.getByText(
        "Gamescope WSI (Restart)",
      ) as HTMLButtonElement;
      expect(compatibility.getAttribute("data-checked")).toBe(
        String(wsiEnabled),
      );
      expect(compatibility.disabled).toBe(false);
      expect(
        screen
          .getByText(
            "Only supported 64-bit host launches. Leave off unless needed; it may reduce performance.",
          )
          .getAttribute("data-tone"),
      ).toBe("warning");
      fireEvent.click(compatibility);
      expect(onConfigChange).toHaveBeenCalledExactlyOnceWith(
        GAMESCOPE_WSI_COMPATIBILITY,
        !wsiEnabled,
      );
    },
  );

  test("starts collapsed and remembers when it is expanded", () => {
    const { container } = render(
      <ConfigurationSection
        config={getDefaults()}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    expect(screen.getByText("External Tools")).toBeTruthy();
    expect(
      screen.queryByText(
        "Optional and per profile. MangoHud and vkBasalt are mutually exclusive with each other, but either can run with Gamescope WSI, Frame Generation, and Scaling. Restart the game after changing the tool.",
      ),
    ).toBeNull();
    expect(
      screen.queryByText(
        "External tools may affect performance. Test each game carefully.",
      ),
    ).toBeNull();
    expect(screen.getByText("Manual Overrides")).toBeTruthy();
    expect(
      screen.queryByText(
        "Optional. MAKO detects these automatically; change them only for custom setups, launchers, or emulators.",
      ),
    ).toBeNull();
    expect(screen.queryByText("Enable MangoHud (Restart)")).toBeNull();
    expect(screen.queryByText("Enable shaders (Restart)")).toBeNull();

    const collapseButton = container.querySelector<HTMLButtonElement>(
      ".MAKO_ExternalToolsCollapseButton_Container button",
    );
    expect(collapseButton).toBeTruthy();
    fireEvent.click(collapseButton!);

    expect(screen.getByText("Enable MangoHud (Restart)")).toBeTruthy();
    expect(screen.queryByText("Enable shaders (Restart)")).toBeNull();
    expect(screen.queryByText("Experimental")).toBeNull();
    expect(localStorage.getItem("mako-external-tools-collapsed")).toBe("false");
  });

  test("offers the MangoHud frame count only while MangoHud is enabled", () => {
    const onConfigChange = vi.fn(async () => undefined);
    const section = (externalLayer: string) => (
      <ConfigurationSection
        groups={["external-tools"]}
        config={{ ...getDefaults(), external_vulkan_layer: externalLayer }}
        onConfigChange={onConfigChange}
        onConfigUpdate={vi.fn(async () => undefined)}
      />
    );
    const { container, rerender } = render(section(""));
    fireEvent.click(
      container.querySelector<HTMLButtonElement>(
        ".MAKO_ExternalToolsCollapseButton_Container button",
      )!,
    );
    expect(screen.queryByText("Count Real Frames Only (Restart)")).toBeNull();

    rerender(section(EXTERNAL_VULKAN_LAYER_MANGOHUD));
    const toggle = screen
      .getByText("Count Real Frames Only (Restart)")
      .closest("button")!;
    expect(toggle.dataset.checked).toBe("false");
    fireEvent.click(toggle);
    expect(onConfigChange).toHaveBeenCalledWith(
      MANGOHUD_COUNT_REAL_FRAMES,
      true,
    );
  });

  test("enables global cadence recovery atomically from Compatibility", () => {
    const onConfigUpdate = vi.fn(async () => undefined);
    const { container } = render(
      <ConfigurationSection
        config={{
          ...getDefaults(),
          adaptive: false,
          adaptive_auto_base_fps_cap: true,
          base_fps_cap: 30,
          dynamic_cadence_recovery: false,
        }}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={onConfigUpdate}
      />,
    );

    const collapseButton = container.querySelector<HTMLButtonElement>(
      ".MAKO_WorkaroundsCollapseButton_Container button",
    );
    fireEvent.click(collapseButton!);
    fireEvent.click(screen.getByText("Dynamic Cadence Recovery"));

    expect(onConfigUpdate).toHaveBeenCalledWith({
      dynamic_cadence_recovery: true,
      gpu_headroom_percent: 0,
      adaptive_auto_base_fps_cap: false,
      adaptive_fractional_real_frame_priority: "auto",
      base_fps_cap: 0,
    });
  });

  test("reveals discrete probe intervals only while Recovery is enabled", () => {
    const onConfigChange = vi.fn(async () => undefined);
    const { container, rerender } = render(
      <ConfigurationSection
        config={{
          ...getDefaults(),
          dynamic_cadence_recovery: false,
          dynamic_cadence_probe_interval_seconds: 2,
        }}
        onConfigChange={onConfigChange}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    const collapseButton = container.querySelector<HTMLButtonElement>(
      ".MAKO_WorkaroundsCollapseButton_Container button",
    );
    fireEvent.click(collapseButton!);
    expect(screen.queryByText("Cadence Probe Interval")).toBeNull();

    rerender(
      <ConfigurationSection
        config={{
          ...getDefaults(),
          dynamic_cadence_recovery: true,
          dynamic_cadence_probe_interval_seconds: 2,
        }}
        onConfigChange={onConfigChange}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    expect(screen.getByText("Cadence Probe Interval")).toBeTruthy();
    const dropdown = screen.getByTestId("cadence-probe-interval-dropdown");
    expect(JSON.parse(dropdown.getAttribute("data-options") || "[]")).toEqual([
      0.1, 0.2, 0.25, 0.5, 0.75, 1, 1.5, 2, 3,
    ]);
    expect(dropdown.textContent).toBe("2s");
    expect(
      screen.getByText(/Recovery check interval/).style.paddingBottom,
    ).toBe("6px");
    fireEvent.click(dropdown);
    expect(onConfigChange).toHaveBeenCalledWith(
      "dynamic_cadence_probe_interval_seconds",
      0.1,
    );

    rerender(
      <ConfigurationSection
        config={{
          ...getDefaults(),
          dynamic_cadence_recovery: true,
          dynamic_cadence_probe_interval_seconds: 0.25,
        }}
        onConfigChange={onConfigChange}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );
    expect(
      screen.getByTestId("cadence-probe-interval-dropdown").textContent,
    ).toBe("0.25s");
  });

  test("preserves a valid non-preset cadence interval in the selector", () => {
    const { container } = render(
      <ConfigurationSection
        config={{
          ...getDefaults(),
          dynamic_cadence_recovery: true,
          dynamic_cadence_probe_interval_seconds: 1.25,
        }}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    const collapseButton = container.querySelector<HTMLButtonElement>(
      ".MAKO_WorkaroundsCollapseButton_Container button",
    );
    fireEvent.click(collapseButton!);

    const dropdown = screen.getByTestId("cadence-probe-interval-dropdown");
    expect(dropdown.textContent).toBe("1.25s");
    expect(JSON.parse(dropdown.getAttribute("data-options") || "[]")).toContain(
      1.25,
    );
  });

  test("enabling the Base FPS Cap turns cadence recovery off", () => {
    const onConfigUpdate = vi.fn(async () => undefined);
    render(
      <ConfigurationSection
        config={{
          ...getDefaults(),
          adaptive: false,
          adaptive_auto_base_fps_cap: false,
          base_fps_cap: 0,
          dynamic_cadence_recovery: true,
        }}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={onConfigUpdate}
      />,
    );

    fireEvent.click(screen.getByText("Base FPS Cap (Off)"));

    expect(onConfigUpdate).toHaveBeenCalledWith({
      base_fps_cap: 30,
      adaptive_fractional_real_frame_priority: "auto",
      dynamic_cadence_recovery: false,
    });
  });

  test("lets explicit Real Frame Priority own the cap while Automatic preserves it", () => {
    const { rerender } = render(
      <ConfigurationSection
        config={{
          ...getDefaults(),
          adaptive: true,
          adaptive_auto_base_fps_cap: false,
          adaptive_fractional_real_frame_priority: "high",
          target_fps: 120,
          base_fps_cap: 35,
        }}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    expect(
      screen.getByText(
        "Controlled by Real Frame Priority (90 FPS). Your manual value remains saved.",
      ),
    ).toBeTruthy();
    expect(
      (
        screen.getByRole("button", {
          name: "Base FPS Cap (35 FPS)",
        }) as HTMLButtonElement
      ).disabled,
    ).toBe(true);

    rerender(
      <ConfigurationSection
        config={{
          ...getDefaults(),
          adaptive: true,
          adaptive_auto_base_fps_cap: false,
          adaptive_fractional_real_frame_priority: "auto",
          target_fps: 120,
          base_fps_cap: 35,
        }}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    expect(
      (
        screen.getByRole("button", {
          name: "Base FPS Cap (35 FPS)",
        }) as HTMLButtonElement
      ).disabled,
    ).toBe(false);
    expect(screen.queryByText(/Controlled by Real Frame Priority/)).toBeNull();
  });

  test("offers manual Base FPS caps through 120 FPS", () => {
    render(
      <ConfigurationSection
        config={getDefaults()}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    expect(
      screen.getByText("Base FPS Cap (Off)").getAttribute("data-maximum"),
    ).toBe("120");
  });

  test("enables and configures the per-profile refresh-rate guard", () => {
    const onConfigChange = vi.fn(async () => undefined);
    const { rerender } = render(
      <ConfigurationSection
        config={getDefaults()}
        onConfigChange={onConfigChange}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    fireEvent.click(
      screen.getByText("Auto-disable Frame Generation by Refresh Rate"),
    );
    expect(onConfigChange).toHaveBeenCalledWith(
      "frame_generation_refresh_threshold",
      60,
    );

    rerender(
      <ConfigurationSection
        config={{
          ...getDefaults(),
          frame_generation_refresh_threshold: 130,
        }}
        onConfigChange={onConfigChange}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );
    fireEvent.click(screen.getByText("Refresh Rate Threshold (130 Hz)"));
    expect(onConfigChange).toHaveBeenLastCalledWith(
      "frame_generation_refresh_threshold",
      131,
    );
  });
});
