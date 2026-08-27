#!/usr/bin/env bash
# End-to-end contract for MAKO CLI language selection and command dispatch.
set -euo pipefail

cli=${1:?mako-cli path is required}
missing_config=/tmp/mako-cli-i18n-missing-config.toml

fail() {
    printf 'mako-cli i18n contract failed: %s\n' "$*" >&2
    exit 1
}

run_failure() {
    set +e
    command_output="$("$@" 2>&1)"
    command_status=$?
    set -e
    [[ $command_status -eq 1 ]] || fail "unexpected status $command_status: $command_output"
}

run_failure "$cli" validate --config "$missing_config"
[[ $command_output == 'Validation failed: configuration file does not exist' ]] ||
    fail "English validation output changed: $command_output"

run_failure "$cli" --lang pt-BR validate --config "$missing_config"
[[ $command_output == 'Falha na validação: arquivo de configuração não existe' ]] ||
    fail "Brazilian Portuguese validation output changed: $command_output"

run_failure "$cli" --lang=pt-PT validate --config "$missing_config"
[[ $command_output == 'Falha na validação: ficheiro de configuração não existe' ]] ||
    fail "European Portuguese validation output changed: $command_output"

run_failure "$cli" --lang es validate --config "$missing_config"
[[ $command_output == 'Falló la validación: el archivo de configuración no existe' ]] ||
    fail "Spanish validation output changed: $command_output"

run_failure "$cli" --lang invalid validate
[[ $command_output == *"unsupported language 'invalid'"* ]] ||
    fail "invalid language was not rejected: $command_output"

run_failure "$cli" --lang
[[ $command_output == *'--lang requires en, pt-BR, pt-PT, or es'* ]] ||
    fail "missing language value was not rejected: $command_output"

help_output="$("$cli" --help 2>&1)" || fail "--help failed: $help_output"
[[ $help_output == *'quality-regression'* ]] ||
    fail "MAKO quality-regression command disappeared from help"
[[ $help_output == *'spatial-quality-regression'* ]] ||
    fail "MAKO spatial-quality-regression command disappeared from help"
[[ $help_output == *'combined-quality-regression'* ]] ||
    fail "MAKO combined-quality-regression command disappeared from help"
[[ $help_output == *'spatial-profile'* ]] ||
    fail "MAKO spatial-profile command disappeared from help"
[[ $help_output == *'synchronization-validation-canary'* ]] ||
    fail "MAKO synchronization-validation-canary command disappeared from help"
[[ $help_output == *'--width <INT>'* && $help_output == *'--height <INT>'* ]] ||
    fail "MAKO exact-resolution options disappeared from help"

run_failure "$cli" quality-regression --scene unknown-scene
[[ $command_output == 'error: unknown quality scene: unknown-scene' ]] ||
    fail "unknown procedural scene did not fail closed: $command_output"

run_failure "$cli" spatial-quality-regression --method unknown-method
[[ $command_output == 'error: unknown spatial quality method: unknown-method' ]] ||
    fail "unknown spatial method did not fail closed: $command_output"

run_failure "$cli" spatial-quality-regression --method native
[[ $command_output == 'error: spatial quality method native is passthrough, not a scaler' ]] ||
    fail "Native passthrough entered the spatial quality scaler path: $command_output"

run_failure "$cli" spatial-profile --method unknown-method
[[ $command_output == 'error: unknown spatial profile method: unknown-method' ]] ||
    fail "unknown spatial profile method did not fail closed: $command_output"

run_failure "$cli" spatial-profile --method native --width +1280 --factor +1.5
[[ $command_output == 'error: spatial profile method native performs no GPU scaling work' ]] ||
    fail "Native passthrough or leading-plus parsing regressed: $command_output"

run_failure "$cli" spatial-profile --method mako --width nope
[[ $command_output == 'error: --width requires a valid finite number: nope' ]] ||
    fail "invalid spatial profile width did not fail cleanly: $command_output"

run_failure "$cli" spatial-profile --method mako --factor nan
[[ $command_output == 'error: --factor requires a valid finite number: nan' ]] ||
    fail "non-finite spatial profile factor did not fail cleanly: $command_output"

run_failure "$cli" combined-quality-regression --method unknown-method
[[ $command_output == 'error: unknown combined quality spatial method: unknown-method' ]] ||
    fail "unknown combined spatial method did not fail closed: $command_output"

run_failure "$cli" combined-quality-regression --method native
[[ $command_output == 'error: combined quality spatial method native is passthrough, not a scaler' ]] ||
    fail "Native passthrough entered the combined quality scaler path: $command_output"

printf 'mako-cli i18n contract: PASS\n'
