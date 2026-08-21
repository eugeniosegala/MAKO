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
    fail "Leviathan quality-regression command disappeared from help"

printf 'mako-cli i18n contract: PASS\n'
