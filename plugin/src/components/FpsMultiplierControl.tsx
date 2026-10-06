import type { ConfigurationEditorProps } from "./settings/types";
import {
  Dropdown,
  Field,
  PanelSectionRow,
  SliderField,
  ToggleField,
} from "@decky/ui";
import {
  useLayoutEffect,
  useRef,
  type ComponentProps,
  type ComponentType,
  type Ref,
} from "react";
import {
  ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_AUTO,
  ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_HIGH,
  ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_LOW,
  ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_MEDIUM,
  ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_VERY_HIGH,
  ADAPTIVE_MINIMUM_BASE_FPS,
  ADAPTIVE_STABLE_CADENCE,
  ADAPTIVE_TARGET_REFRESH_RATE,
  type AdaptiveFractionalRealFramePriority,
  FRAME_GENERATION_ENABLED,
  FRAME_GENERATION_PROVISIONED,
  GAMESCOPE_VRR_MODE,
  getDefaults,
  PERFORMANCE_MODE,
  TARGET_FPS,
  TARGET_FPS_MAX,
  TARGET_FPS_MIN,
} from "../config/configSchema";
import {
  adaptiveModeChanges,
  fractionalAdaptivePresetChanges,
  fractionalRealFramePriorityCap,
  fractionalRealFramePriorityChanges,
  isFractionalAdaptivePresetEnabled,
  steadyBaseCapChanges,
} from "../config/fractionalAdaptivePreset";
import t from "../i18n/i18n";
import { MakoRestartLabel, MakoSettingRelationship } from "./MakoUi";

const DEFAULT_CONFIGURATION = getDefaults();
const GENERATION_MULTIPLIER_CHOICES = [0, 2, 3, 4, 5] as const;
const GENERATION_MULTIPLIER_SLIDER_MAX =
  GENERATION_MULTIPLIER_CHOICES.length - 1;
const GENERATION_MULTIPLIER_FOCUS_SELECTOR =
  "[role='slider'], [role='button'], button, input, select, [tabindex]:not([tabindex='-1'])";

interface SteamSliderNavigationHandle {
  BFocusWithin(): boolean;
  ChildTakeFocus(): boolean;
  TakeFocus(): boolean;
}

type SteamSliderFieldProps = ComponentProps<typeof SliderField> & {
  navRef?: Ref<SteamSliderNavigationHandle>;
};

// Steam supports navRef here even though Decky's SliderField type omits it.
const SteamSliderField = SliderField as ComponentType<SteamSliderFieldProps>;

function multiplierSliderPosition(multiplier: number): number {
  const position = GENERATION_MULTIPLIER_CHOICES.findIndex(
    (choice) => choice === multiplier,
  );
  return position >= 0 ? position : 1;
}

function multiplierAtSliderPosition(position: number): number | undefined {
  if (
    !Number.isInteger(position) ||
    position < 0 ||
    position > GENERATION_MULTIPLIER_SLIDER_MAX
  ) {
    return undefined;
  }
  return GENERATION_MULTIPLIER_CHOICES[position];
}

export function FpsMultiplierControl({
  config,
  onConfigChange,
  onConfigUpdate,
}: ConfigurationEditorProps) {
  const pendingMultiplierFocus = useRef<{
    mode: "adaptive" | "fixed";
    ownerDocument: Document;
  }>();
  const adaptiveMultiplierNavigation =
    useRef<SteamSliderNavigationHandle>(null);
  const fixedMultiplierNavigation = useRef<SteamSliderNavigationHandle>(null);
  const targetFps = config.target_fps;
  const adaptiveMaxMultiplier =
    config.adaptive_max_multiplier ??
    DEFAULT_CONFIGURATION.adaptive_max_multiplier;
  const frameGenerationEnabled =
    config.frame_generation_enabled ??
    DEFAULT_CONFIGURATION.frame_generation_enabled;
  const frameGenerationProvisioned =
    config.frame_generation_provisioned ??
    DEFAULT_CONFIGURATION.frame_generation_provisioned;
  const fixedMultiplier = config.multiplier ?? DEFAULT_CONFIGURATION.multiplier;
  const automaticBaseFpsCap = Math.max(
    ADAPTIVE_MINIMUM_BASE_FPS,
    targetFps / 2,
  );
  const automaticBaseFpsCapLabel = Number.isInteger(automaticBaseFpsCap)
    ? automaticBaseFpsCap.toFixed(0)
    : automaticBaseFpsCap.toFixed(1);
  const fractionalAdaptive = isFractionalAdaptivePresetEnabled(config);
  const fractionalPriority = (config.adaptive_fractional_real_frame_priority ??
    ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_AUTO) as AdaptiveFractionalRealFramePriority;
  const fractionalPriorityCap = fractionalRealFramePriorityCap(
    targetFps,
    fractionalPriority,
  );
  const fractionalPriorityCapLabel =
    fractionalPriorityCap === undefined
      ? undefined
      : Number.isInteger(fractionalPriorityCap)
        ? fractionalPriorityCap.toFixed(0)
        : fractionalPriorityCap.toFixed(1);
  const fractionalGeneratedFps =
    fractionalPriorityCap === undefined
      ? undefined
      : targetFps - fractionalPriorityCap;
  const fractionalGeneratedFpsLabel =
    fractionalGeneratedFps === undefined
      ? undefined
      : Number.isInteger(fractionalGeneratedFps)
        ? fractionalGeneratedFps.toFixed(0)
        : fractionalGeneratedFps.toFixed(1);
  const fractionalPriorityOptions = [
    {
      data: ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_AUTO,
      label: t("ADAPTIVE_REAL_FRAME_PRIORITY_AUTO", "Automatic"),
    },
    ...(
      [
        [
          ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_LOW,
          t("ADAPTIVE_REAL_FRAME_PRIORITY_LOW", "Low"),
        ],
        [
          ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_MEDIUM,
          t("ADAPTIVE_REAL_FRAME_PRIORITY_MEDIUM", "Medium"),
        ],
        [
          ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_HIGH,
          t("ADAPTIVE_REAL_FRAME_PRIORITY_HIGH", "High"),
        ],
        [
          ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_VERY_HIGH,
          t("ADAPTIVE_REAL_FRAME_PRIORITY_VERY_HIGH", "Very High"),
        ],
      ] as const
    ).map(([data, priority]) => {
      const cap = fractionalRealFramePriorityCap(targetFps, data)!;
      return {
        data,
        label: t(
          "ADAPTIVE_REAL_FRAME_PRIORITY_OPTION",
          "{priority} — up to {cap} real FPS ({percent}% of target)",
          {
            priority,
            cap: Number(cap.toFixed(1)),
            percent: Math.round((cap / targetFps) * 100),
          },
        ),
      };
    }),
  ];
  const fractionalPriorityRatio =
    fractionalPriority === ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_LOW
      ? { real: 3, generated: 2 }
      : fractionalPriority === ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_MEDIUM
        ? { real: 2, generated: 1 }
        : fractionalPriority === ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_HIGH
          ? { real: 3, generated: 1 }
          : fractionalPriority ===
              ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_VERY_HIGH
            ? { real: 4, generated: 1 }
            : undefined;

  useLayoutEffect(() => {
    const pending = pendingMultiplierFocus.current;
    if (!pending) return;
    const navigation =
      pending.mode === "adaptive"
        ? adaptiveMultiplierNavigation.current
        : fixedMultiplierNavigation.current;
    if (navigation?.ChildTakeFocus() || navigation?.TakeFocus()) {
      pendingMultiplierFocus.current = undefined;
      return;
    }
    const container = pending.ownerDocument.querySelector<HTMLElement>(
      `.Mako_GenerationMultiplierSlider--${pending.mode}`,
    );
    const target = container?.matches(GENERATION_MULTIPLIER_FOCUS_SELECTOR)
      ? container
      : container?.querySelector<HTMLElement>(
          GENERATION_MULTIPLIER_FOCUS_SELECTOR,
        );
    target?.focus({ preventScroll: true });
    pendingMultiplierFocus.current = undefined;
  }, [frameGenerationEnabled]);

  const retainMultiplierFocus = (
    mode: "adaptive" | "fixed",
    nextEnabled: boolean,
  ) => {
    if (nextEnabled === frameGenerationEnabled) return;
    const navigation =
      mode === "adaptive"
        ? adaptiveMultiplierNavigation.current
        : fixedMultiplierNavigation.current;
    const activeElement = document.activeElement as HTMLElement | null;
    const container = activeElement?.closest(
      `.Mako_GenerationMultiplierSlider--${mode}`,
    );
    if (navigation?.BFocusWithin() || (container && activeElement)) {
      pendingMultiplierFocus.current = {
        mode,
        ownerDocument: activeElement?.ownerDocument ?? document,
      };
    }
  };

  return (
    <>
      <PanelSectionRow>
        <ToggleField
          label={
            <MakoRestartLabel
              label={t(
                "FRAME_GENERATION_PROVISIONED",
                "Enable Frame-gen (Restart)",
              )}
            />
          }
          description={t(
            "FRAME_GENERATION_PROVISIONED_DESC",
            "Enable before launch to load Frame Generation; turn off for Scaling or Shaders only.",
          )}
          checked={frameGenerationProvisioned}
          bottomSeparator={frameGenerationProvisioned ? undefined : "none"}
          onChange={(value) =>
            onConfigChange(FRAME_GENERATION_PROVISIONED, value)
          }
        />
      </PanelSectionRow>

      {frameGenerationProvisioned && (
        <>
          <PanelSectionRow>
            <ToggleField
              label={t("ADAPTIVE_TITLE", "Adaptive Frame Generation")}
              description={t(
                "ADAPTIVE_DESC",
                "Targets output FPS. Steady Base Cap favors smoother pacing by default; Fractional Adaptive keeps more real frames. Test per game.",
              )}
              checked={config.adaptive}
              onChange={(value) => onConfigUpdate(adaptiveModeChanges(value))}
            />
          </PanelSectionRow>

          {config.adaptive && (
            <>
              <PanelSectionRow>
                <ToggleField
                  label={t("FRACTIONAL_ADAPTIVE_PRESET", "Fractional Adaptive")}
                  description={
                    <>
                      <div>
                        {t(
                          "FRACTIONAL_ADAPTIVE_PRESET_DESC",
                          "Mixes generation ratios to reach targets such as 60 real FPS → 90 displayed FPS. It keeps more real frames and may reduce input lag and ghosting, but can feel less smooth in some games.",
                        )}
                      </div>
                      <MakoSettingRelationship>
                        {t(
                          "FRACTIONAL_ADAPTIVE_PRESET_RELATION",
                          "Cannot be combined with Steady Base Cap. Changing it also turns Dynamic Cadence Recovery off.",
                        )}
                      </MakoSettingRelationship>
                    </>
                  }
                  checked={fractionalAdaptive}
                  onChange={(value) =>
                    onConfigUpdate(fractionalAdaptivePresetChanges(value))
                  }
                />
              </PanelSectionRow>
              {fractionalAdaptive && (
                <PanelSectionRow>
                  <Field
                    label={t(
                      "ADAPTIVE_REAL_FRAME_PRIORITY",
                      "Real Frame Priority",
                    )}
                    description={
                      <>
                        <div>
                          {t(
                            "ADAPTIVE_REAL_FRAME_PRIORITY_DESC",
                            "The shown FPS values estimate real-frame caps from Target FPS, not rates a game is guaranteed to deliver. Higher priority allows more real frames and may reduce latency and ghosting, but can feel less even.",
                          )}
                        </div>
                        <MakoSettingRelationship>
                          {fractionalPriorityRatio &&
                          fractionalPriorityCapLabel !== undefined &&
                          fractionalGeneratedFpsLabel !== undefined
                            ? t(
                                "ADAPTIVE_REAL_FRAME_PRIORITY_ACTIVE_RELATION",
                                "At a {target} FPS target: about {cap} real / {generated_fps} generated FPS ({real}:{generated}) if reached. Actual rates vary; this overrides Base FPS Cap.",
                                {
                                  real: fractionalPriorityRatio.real,
                                  generated: fractionalPriorityRatio.generated,
                                  target: targetFps,
                                  cap: fractionalPriorityCapLabel,
                                  generated_fps: fractionalGeneratedFpsLabel,
                                },
                              )
                            : t(
                                "ADAPTIVE_REAL_FRAME_PRIORITY_AUTO_RELATION",
                                "Automatic keeps Fractional Adaptive's current behavior. Base FPS Cap remains available.",
                              )}
                        </MakoSettingRelationship>
                      </>
                    }
                    childrenLayout="below"
                    childrenContainerWidth="max"
                  >
                    <Dropdown
                      rgOptions={fractionalPriorityOptions}
                      selectedOption={fractionalPriority}
                      onChange={(option) =>
                        onConfigUpdate(
                          fractionalRealFramePriorityChanges(
                            option.data as AdaptiveFractionalRealFramePriority,
                          ),
                        )
                      }
                    />
                  </Field>
                </PanelSectionRow>
              )}
              <PanelSectionRow>
                <SliderField
                  label={`${
                    config.adaptive_target_refresh_rate
                      ? t("ADAPTIVE_FALLBACK_TARGET_FPS", "Fallback Target FPS")
                      : t("ADAPTIVE_TARGET_FPS", "Target FPS")
                  } (${targetFps})`}
                  description={
                    config.adaptive_target_refresh_rate
                      ? t(
                          "ADAPTIVE_FALLBACK_TARGET_FPS_DESC",
                          "Used if refresh detection fails. Turn matching off to edit.",
                        )
                      : t(
                          "ADAPTIVE_TARGET_FPS_DESC",
                          "Desired output FPS. Fractional may mix ratios; Steady Base Cap starts at half the target and can align a validated lower integer ratio.",
                        )
                  }
                  value={targetFps}
                  disabled={config.adaptive_target_refresh_rate ?? false}
                  min={TARGET_FPS_MIN}
                  max={TARGET_FPS_MAX}
                  step={1}
                  onChange={(value) => onConfigChange(TARGET_FPS, value)}
                />
              </PanelSectionRow>
              <PanelSectionRow>
                <ToggleField
                  label={t(
                    "ADAPTIVE_TARGET_REFRESH_RATE",
                    "Match Display Refresh Rate",
                  )}
                  description={t(
                    "ADAPTIVE_TARGET_REFRESH_RATE_DESC",
                    "Uses the display refresh rate as Target FPS.",
                  )}
                  checked={config.adaptive_target_refresh_rate ?? false}
                  onChange={(value) =>
                    onConfigChange(ADAPTIVE_TARGET_REFRESH_RATE, value)
                  }
                />
              </PanelSectionRow>
              <PanelSectionRow>
                <ToggleField
                  label={`${t("ADAPTIVE_AUTO_BASE_FPS_CAP", "Steady Base Cap")} (${automaticBaseFpsCapLabel} FPS)`}
                  description={
                    <>
                      <div>
                        {t(
                          "ADAPTIVE_AUTO_BASE_FPS_CAP_DESC",
                          "Default Adaptive mode: starts at half the target; Smooth Cadence can align validated 3x–5x ratios. Usually smoother, with fewer real frames and possible extra lag or ghosting.",
                        )}
                      </div>
                      <MakoSettingRelationship>
                        {t(
                          "ADAPTIVE_AUTO_BASE_FPS_CAP_RELATION",
                          "Overrides Base FPS Cap. Cannot be combined with Fractional Adaptive or Dynamic Cadence Recovery.",
                        )}
                      </MakoSettingRelationship>
                    </>
                  }
                  checked={
                    config.adaptive_auto_base_fps_cap ??
                    DEFAULT_CONFIGURATION.adaptive_auto_base_fps_cap
                  }
                  onChange={(value) =>
                    onConfigUpdate(steadyBaseCapChanges(value))
                  }
                />
              </PanelSectionRow>
              <PanelSectionRow>
                <SteamSliderField
                  key={`adaptive-${frameGenerationEnabled ? "active" : "paused"}`}
                  navRef={adaptiveMultiplierNavigation}
                  className="Mako_GenerationMultiplierSlider Mako_GenerationMultiplierSlider--adaptive"
                  label={`${t("ADAPTIVE_MAX_MULTIPLIER", "Maximum Adaptive Multiplier")} (${frameGenerationEnabled ? adaptiveMaxMultiplier : 0}x)`}
                  description={
                    <span style={{ display: "block", paddingBottom: "2px" }}>
                      {t(
                        "ADAPTIVE_MAX_MULTIPLIER_DESC",
                        "Use 0x to pause or resume Frame Generation live without unloading its resources. Otherwise this is the interpolation ceiling, not a fixed ratio; Adaptive may use lower or fractional multipliers. Set it only as high as needed to reach Target FPS. Test 2x–5x per game.",
                      )}
                    </span>
                  }
                  value={
                    frameGenerationEnabled
                      ? multiplierSliderPosition(adaptiveMaxMultiplier)
                      : 0
                  }
                  min={0}
                  max={GENERATION_MULTIPLIER_SLIDER_MAX}
                  step={1}
                  validValues="steps"
                  minimumDpadGranularity={1}
                  notchCount={GENERATION_MULTIPLIER_CHOICES.length}
                  notchTicksVisible
                  onChange={(position) => {
                    const value = multiplierAtSliderPosition(position);
                    if (value === 0) {
                      retainMultiplierFocus("adaptive", false);
                      void onConfigChange(FRAME_GENERATION_ENABLED, false);
                    } else if (value !== undefined) {
                      retainMultiplierFocus("adaptive", true);
                      void onConfigUpdate({
                        frame_generation_enabled: true,
                        adaptive_max_multiplier: value,
                      });
                    }
                  }}
                />
              </PanelSectionRow>
            </>
          )}

          {!config.adaptive && (
            <PanelSectionRow>
              <SteamSliderField
                key={`fixed-${frameGenerationEnabled ? "active" : "paused"}`}
                navRef={fixedMultiplierNavigation}
                className="Mako_GenerationMultiplierSlider Mako_GenerationMultiplierSlider--fixed"
                label={`${t("FIXED_MULTIPLIER", "Fixed Multiplier")} (${frameGenerationEnabled ? fixedMultiplier : 0}x)`}
                description={t(
                  "FIXED_MULTIPLIER_DESC",
                  "2x–5x sets a constant output ratio; 5x costs more and suits high-refresh displays. 0x pauses generation live without unloading resources.",
                )}
                value={
                  frameGenerationEnabled
                    ? multiplierSliderPosition(fixedMultiplier)
                    : 0
                }
                min={0}
                max={GENERATION_MULTIPLIER_SLIDER_MAX}
                step={1}
                validValues="steps"
                minimumDpadGranularity={1}
                notchCount={GENERATION_MULTIPLIER_CHOICES.length}
                notchTicksVisible
                onChange={(position) => {
                  const value = multiplierAtSliderPosition(position);
                  if (value === 0) {
                    retainMultiplierFocus("fixed", false);
                    void onConfigChange(FRAME_GENERATION_ENABLED, false);
                  } else if (value !== undefined) {
                    retainMultiplierFocus("fixed", true);
                    void onConfigUpdate({
                      frame_generation_enabled: true,
                      multiplier: value,
                    });
                  }
                }}
              />
            </PanelSectionRow>
          )}

          <PanelSectionRow>
            <ToggleField
              label={t("ADAPTIVE_SMOOTH_CADENCE", "Smooth Cadence")}
              description={t(
                "ADAPTIVE_SMOOTH_CADENCE_DESC",
                "Uses validated ordered Gamescope presentation for steadier pacing. Fractional Adaptive keeps real frames; Fixed and Steady Base Cap can favor even output. It may reduce real FPS and responsiveness. On by default; turn it off per game if preferred.",
              )}
              checked={
                config.adaptive_stable_cadence ??
                DEFAULT_CONFIGURATION.adaptive_stable_cadence
              }
              onChange={(value) =>
                onConfigChange(ADAPTIVE_STABLE_CADENCE, value)
              }
            />
          </PanelSectionRow>

          <PanelSectionRow>
            <Field
              label={t("GAMESCOPE_VRR_MODE", "Gamescope VRR")}
              description={t(
                "GAMESCOPE_VRR_MODE_DESC",
                "Disabling VRR lets MAKO control frame pacing. This can improve frame generation in some games but not others, so test it per game. If your device or display does not support VRR, this setting has no effect.",
              )}
              childrenLayout="below"
              childrenContainerWidth="max"
            >
              <Dropdown
                rgOptions={[
                  {
                    data: "follow-steam",
                    label: t("GAMESCOPE_VRR_FOLLOW_STEAM", "Follow Steam"),
                  },
                  { data: "on", label: t("GAMESCOPE_VRR_ON", "On") },
                  { data: "off", label: t("GAMESCOPE_VRR_OFF", "Off") },
                ]}
                selectedOption={config.gamescope_vrr_mode ?? "follow-steam"}
                onChange={(option) =>
                  onConfigChange(
                    GAMESCOPE_VRR_MODE,
                    option.data as "follow-steam" | "on" | "off",
                  )
                }
              />
            </Field>
          </PanelSectionRow>

          <PanelSectionRow>
            <ToggleField
              label={t("CONFIG_PERFORMANCE_MODE", "Lighter FG Model")}
              description={t(
                "CONFIG_PERFORMANCE_MODE_DESC",
                "Lighter FG model lowers GPU cost but increases ghosting; Ultra Performance forces it on.",
              )}
              checked={config.ultra_performance || config.performance_mode}
              disabled={config.ultra_performance}
              onChange={(value) => onConfigChange(PERFORMANCE_MODE, value)}
              bottomSeparator="none"
            />
          </PanelSectionRow>
        </>
      )}
    </>
  );
}
