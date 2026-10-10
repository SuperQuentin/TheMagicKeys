@echo off
setlocal

SET INPUT_WAV_PATH="VCSL\Chordophones\Composite Chordophones\Concert Harp"
SET WAV_FILES_FILTER="*_f1.wav"
SET OUTPUT_DIR_NAME=output_harp
SET CUTOFF_THRESHOLD_DB=-60

REM Activate the project virtual environment if it exists.
IF EXIST ".venv\Scripts\activate.bat" (
    call ".venv\Scripts\activate.bat"
) ELSE (
    echo Warning: .venv not found. Continuing with the current Python environment.
)

REM Step 1: build the partial note-to-sample mapping
python generate_partial_mapping.py %INPUT_WAV_PATH% %WAV_FILES_FILTER% partial_mapping.csv

REM Step 2: fill missing notes using the nearest available sample
python generate_full_mapping.py partial_mapping.csv full_mapping.csv

REM Step 3: generate the final output wav files
python generate_all_samples.py full_mapping.csv %OUTPUT_DIR_NAME% --threshold_db %CUTOFF_THRESHOLD_DB%

REM Step4: Copy some useful files as documentation
MKDIR %OUTPUT_DIR_NAME%\doc
COPY partial_mapping.csv %OUTPUT_DIR_NAME%\doc
COPY full_mapping.csv %OUTPUT_DIR_NAME%\doc
COPY run_generation.bat %OUTPUT_DIR_NAME%\doc

ECHO Generation complete.
endlocal
