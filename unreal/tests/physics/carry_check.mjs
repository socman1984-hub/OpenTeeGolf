// The game's ball flight against OpenFairway's reference launches (carry_main.cpp): builds it with Visual
// Studio's compiler and prints carry, apex and roll beside the references.
//
//   node unreal/tests/physics/carry_check.mjs

import fs from 'node:fs';
import path from 'node:path';
import { execFileSync, execSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const SRC = path.resolve(HERE, '../../OpenTeeGolf/Plugins/OTGBallPhysics/Source/OTGBallPhysics');
const OUT = path.join(HERE, 'build');
const EXE = path.join(OUT, 'carry.exe');

fs.mkdirSync(OUT, { recursive: true });
const vswhere = 'C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe';
const vs = execFileSync(vswhere, ['-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath']).toString().trim();
const vcvars = path.join(vs, 'VC/Auxiliary/Build/vcvars64.bat');
const cmd = `call "${vcvars}" >nul && cl /nologo /std:c++17 /O2 /EHsc /fp:precise /I "${path.join(SRC, 'Public')}" "${path.join(SRC, 'Private/Physics/OTGBallPhysics.cpp')}" "${path.join(HERE, 'carry_main.cpp')}" /Fe:"${EXE}" /Fo:"${OUT}\\\\"`;
execSync(`cmd /d /s /c "${cmd}"`, { stdio: ['ignore', 'pipe', 'inherit'], cwd: OUT });
process.stdout.write(execFileSync(EXE).toString());
