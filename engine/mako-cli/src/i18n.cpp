/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "i18n.hpp"

using namespace mako::cli::i18n;

namespace {
    constexpr Strings english{
        .validation_success = "Validation success",
        .validation_missing_file = "Validation failed: configuration file does not exist",
        .validation_failed = "Validation failed: ",
        .benchmark_results = "benchmark results (ran for ",
        .benchmark_iterations = "  iterations:       ",
        .benchmark_generated_frames = "  generated frames: ",
        .benchmark_total_frames = "  total frames:     ",
        .benchmark_generated_fps = "  fps (generated):  ",
        .benchmark_total_fps = "  fps (total):      ",
        .benchmark_seconds = " seconds):\n",
        .error = "error: ",
        .flow_scale_range = "flow scale must be between 0.25 and 1.0",
        .multiplier_minimum = "multiplier must be 2 or greater",
        .dimensions_positive = "width and height must be positive integers",
        .duration_positive = "duration must be a positive integer",
        .gpu_not_found = "failed to find specified GPU: ",
        .debug_path_missing = "debug path does not exist: ",
        .debug_filename_invalid = "invalid debug file name: ",
        .frame_wait_failed = "failed to wait for frame",
        .debug_image_open_failed = "failed to open debug image",
        .debug_image_read_failed = "failed to read debug image",
    };

    constexpr Strings portuguese_brazil{
        .validation_success = "Validação concluída com sucesso",
        .validation_missing_file = "Falha na validação: arquivo de configuração não existe",
        .validation_failed = "Falha na validação: ",
        .benchmark_results = "resultados do benchmark (executado por ",
        .benchmark_iterations = "  iterações:        ",
        .benchmark_generated_frames = "  quadros gerados:  ",
        .benchmark_total_frames = "  quadros totais:   ",
        .benchmark_generated_fps = "  fps (gerados):    ",
        .benchmark_total_fps = "  fps (total):      ",
        .benchmark_seconds = " segundos):\n",
        .error = "erro: ",
        .flow_scale_range = "a escala de fluxo deve estar entre 0.25 e 1.0",
        .multiplier_minimum = "o multiplicador deve ser 2 ou maior",
        .dimensions_positive = "largura e altura devem ser números inteiros positivos",
        .duration_positive = "a duração deve ser um número inteiro positivo",
        .gpu_not_found = "falha ao encontrar a GPU especificada: ",
        .debug_path_missing = "caminho de depuração não existe: ",
        .debug_filename_invalid = "nome de arquivo de depuração inválido: ",
        .frame_wait_failed = "falha ao aguardar o quadro",
        .debug_image_open_failed = "falha ao abrir a imagem de depuração",
        .debug_image_read_failed = "falha ao ler a imagem de depuração",
    };

    constexpr Strings portuguese_portugal{
        .validation_success = "Validação concluída com sucesso",
        .validation_missing_file = "Falha na validação: ficheiro de configuração não existe",
        .validation_failed = "Falha na validação: ",
        .benchmark_results = "resultados do benchmark (executado durante ",
        .benchmark_iterations = "  iterações:        ",
        .benchmark_generated_frames = "  fotogramas gerados: ",
        .benchmark_total_frames = "  fotogramas totais:  ",
        .benchmark_generated_fps = "  fps (gerados):      ",
        .benchmark_total_fps = "  fps (total):        ",
        .benchmark_seconds = " segundos):\n",
        .error = "erro: ",
        .flow_scale_range = "a escala de fluxo deve estar entre 0.25 e 1.0",
        .multiplier_minimum = "o multiplicador deve ser 2 ou superior",
        .dimensions_positive = "a largura e a altura devem ser números inteiros positivos",
        .duration_positive = "a duração deve ser um número inteiro positivo",
        .gpu_not_found = "falha ao encontrar a GPU especificada: ",
        .debug_path_missing = "o caminho de depuração não existe: ",
        .debug_filename_invalid = "nome de ficheiro de depuração inválido: ",
        .frame_wait_failed = "falha ao aguardar o fotograma",
        .debug_image_open_failed = "falha ao abrir a imagem de depuração",
        .debug_image_read_failed = "falha ao ler a imagem de depuração",
    };

    constexpr Strings spanish{
        .validation_success = "Validación completada correctamente",
        .validation_missing_file = "Falló la validación: el archivo de configuración no existe",
        .validation_failed = "Falló la validación: ",
        .benchmark_results = "resultados del benchmark (ejecutado durante ",
        .benchmark_iterations = "  iteraciones:        ",
        .benchmark_generated_frames = "  fotogramas generados: ",
        .benchmark_total_frames = "  fotogramas totales:  ",
        .benchmark_generated_fps = "  fps (generados):     ",
        .benchmark_total_fps = "  fps (total):         ",
        .benchmark_seconds = " segundos):\n",
        .error = "error: ",
        .flow_scale_range = "la escala de flujo debe estar entre 0.25 y 1.0",
        .multiplier_minimum = "el multiplicador debe ser 2 o mayor",
        .dimensions_positive = "el ancho y el alto deben ser números enteros positivos",
        .duration_positive = "la duración debe ser un número entero positivo",
        .gpu_not_found = "no se pudo encontrar la GPU especificada: ",
        .debug_path_missing = "la ruta de depuración no existe: ",
        .debug_filename_invalid = "nombre de archivo de depuración no válido: ",
        .frame_wait_failed = "falló la espera del fotograma",
        .debug_image_open_failed = "no se pudo abrir la imagen de depuración",
        .debug_image_read_failed = "no se pudo leer la imagen de depuración",
    };
}

const Strings& mako::cli::i18n::strings(const Language language) noexcept {
    switch (language) {
        case Language::English:
            return english;
        case Language::PortugueseBrazil:
            return portuguese_brazil;
        case Language::PortuguesePortugal:
            return portuguese_portugal;
        case Language::Spanish:
            return spanish;
    }
    return english;
}

std::optional<Language> mako::cli::i18n::language_from_code(
        const std::string_view code_value) noexcept {
    if (code_value == "en")
        return Language::English;
    if (code_value == "pt-BR")
        return Language::PortugueseBrazil;
    if (code_value == "pt-PT")
        return Language::PortuguesePortugal;
    if (code_value == "es")
        return Language::Spanish;
    return std::nullopt;
}

std::string_view mako::cli::i18n::code(const Language language) noexcept {
    switch (language) {
        case Language::English:
            return supported_language_codes.at(0);
        case Language::PortugueseBrazil:
            return supported_language_codes.at(1);
        case Language::PortuguesePortugal:
            return supported_language_codes.at(2);
        case Language::Spanish:
            return supported_language_codes.at(3);
    }
    return supported_language_codes.front();
}
