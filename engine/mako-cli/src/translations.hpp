/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <string>
#include <unordered_map>

namespace mako::cli::i18n {

    enum class Lang { En, PtBr, PtPt, Es };

    struct Strings {
        std::string validateSuccess;
        std::string validateFailNoFile;
        std::string validateFailError;
        std::string benchResults;
        std::string benchIterations;
        std::string benchGeneratedFrames;
        std::string benchTotalFrames;
        std::string benchFpsGenerated;
        std::string benchFpsTotal;
        std::string benchSeconds;
        std::string error;
        std::string flowScaleRange;
        std::string multiplierMin;
        std::string dimensionsPositive;
        std::string durationPositive;
        std::string gpuNotFound;
        std::string debugPathNotExist;
        std::string debugInvalidFileName;
        std::string frameWaitFailed;
        std::string dllReadFailed;
        std::string dllReadCodeFailed;
    };

    inline const Strings& get(Lang lang) {
        static const Strings en{
            .validateSuccess = "Validation success",
            .validateFailNoFile = "Validation failed: configuration file does not exist",
            .validateFailError = "Validation failed: ",
            .benchResults = "benchmark results (ran for ",
            .benchIterations = "  iterations:       ",
            .benchGeneratedFrames = "  generated frames: ",
            .benchTotalFrames = "  total frames:     ",
            .benchFpsGenerated = "  fps (generated):  ",
            .benchFpsTotal = "  fps (total):      ",
            .benchSeconds = " seconds):\n",
            .error = "error: ",
            .flowScaleRange = "flow scale must be between 0.25 and 1.0",
            .multiplierMin = "multiplier must be 2 or greater",
            .dimensionsPositive = "width and height must be positive integers",
            .durationPositive = "duration must be a positive integer",
            .gpuNotFound = "failed to find specified GPU: ",
            .debugPathNotExist = "debug path does not exist: ",
            .debugInvalidFileName = "invalid debug file name: ",
            .frameWaitFailed = "failed to wait for frame",
            .dllReadFailed = "ifstream::ifstream() failed",
            .dllReadCodeFailed = "ifstream::read() failed",
        };

        static const Strings ptBr{
            .validateSuccess = "Validação concluída com sucesso",
            .validateFailNoFile = "Falha na validação: arquivo de configuração não existe",
            .validateFailError = "Falha na validação: ",
            .benchResults = "resultados do benchmark (executado por ",
            .benchIterations = "  iterações:        ",
            .benchGeneratedFrames = "  quadros gerados:  ",
            .benchTotalFrames = "  quadros totais:   ",
            .benchFpsGenerated = "  fps (gerados):    ",
            .benchFpsTotal = "  fps (total):      ",
            .benchSeconds = " segundos):\n",
            .error = "erro: ",
            .flowScaleRange = "a escala de fluxo deve estar entre 0.25 e 1.0",
            .multiplierMin = "o multiplicador deve ser 2 ou maior",
            .dimensionsPositive = "largura e altura devem ser números inteiros positivos",
            .durationPositive = "a duração deve ser um número inteiro positivo",
            .gpuNotFound = "falha ao encontrar a GPU especificada: ",
            .debugPathNotExist = "caminho de depuração não existe: ",
            .debugInvalidFileName = "nome de arquivo de depuração inválido: ",
            .frameWaitFailed = "falha ao aguardar o quadro",
            .dllReadFailed = "ifstream::ifstream() falhou",
            .dllReadCodeFailed = "ifstream::read() falhou",
        };

        static const Strings ptPt{
            .validateSuccess = "Validação concluída com sucesso",
            .validateFailNoFile = "Falha na validação: ficheiro de configuração não existe",
            .validateFailError = "Falha na validação: ",
            .benchResults = "resultados do benchmark (executado durante ",
            .benchIterations = "  iterações:        ",
            .benchGeneratedFrames = "  quadros gerados:  ",
            .benchTotalFrames = "  quadros totais:   ",
            .benchFpsGenerated = "  fps (gerados):    ",
            .benchFpsTotal = "  fps (total):      ",
            .benchSeconds = " segundos):\n",
            .error = "erro: ",
            .flowScaleRange = "a escala de fluxo deve estar entre 0.25 e 1.0",
            .multiplierMin = "o multiplicador deve ser 2 ou superior",
            .dimensionsPositive = "largura e altura devem ser números inteiros positivos",
            .durationPositive = "a duração deve ser um número inteiro positivo",
            .gpuNotFound = "falha ao encontrar a GPU especificada: ",
            .debugPathNotExist = "caminho de depuração não existe: ",
            .debugInvalidFileName = "nome de ficheiro de depuração inválido: ",
            .frameWaitFailed = "falha ao aguardar o quadro",
            .dllReadFailed = "ifstream::ifstream() falhou",
            .dllReadCodeFailed = "ifstream::read() falhou",
        };

        static const Strings es{
            .validateSuccess = "Validación exitosa",
            .validateFailNoFile = "Falló la validación: el archivo de configuración no existe",
            .validateFailError = "Falló la validación: ",
            .benchResults = "resultados del benchmark (ejecutado durante ",
            .benchIterations = "  iteraciones:      ",
            .benchGeneratedFrames = "  cuadros generados:",
            .benchTotalFrames = "  cuadros totales:  ",
            .benchFpsGenerated = "  fps (generados):  ",
            .benchFpsTotal = "  fps (total):      ",
            .benchSeconds = " segundos):\n",
            .error = "error: ",
            .flowScaleRange = "la escala de flujo debe estar entre 0.25 y 1.0",
            .multiplierMin = "el multiplicador debe ser 2 o mayor",
            .dimensionsPositive = "el ancho y alto deben ser números enteros positivos",
            .durationPositive = "la duración debe ser un número entero positivo",
            .gpuNotFound = "no se pudo encontrar la GPU especificada: ",
            .debugPathNotExist = "la ruta de depuración no existe: ",
            .debugInvalidFileName = "nombre de archivo de depuración inválido: ",
            .frameWaitFailed = "falló al esperar el cuadro",
            .dllReadFailed = "ifstream::ifstream() falló",
            .dllReadCodeFailed = "ifstream::read() falló",
        };

        switch (lang) {
            case Lang::En:   return en;
            case Lang::PtBr: return ptBr;
            case Lang::PtPt: return ptPt;
            case Lang::Es:   return es;
        }
        return en;
    }

    inline Lang parseLang(const std::string& s) {
        if (s == "en")   return Lang::En;
        if (s == "pt-BR") return Lang::PtBr;
        if (s == "pt-PT") return Lang::PtPt;
        if (s == "es")   return Lang::Es;
        return Lang::En;
    }

}
