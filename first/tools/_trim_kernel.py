import shutil
fb = open(r'e:\project\clion\project_system\first\build-mingw\kernel_full.bin','rb').read()
m = bytes([0xd6,0x50,0x52,0xe8])
idx = fb.find(m)
if idx < 0: raise SystemExit('magic not found')
out = fb[idx:]
open(r'e:\project\clion\project_system\first\output\kernel.bin','wb').write(out)
shutil.copy(r'e:\project\clion\project_system\first\output\kernel.bin',
            r'e:\project\clion\project_system\first\output\kernel_clean.bin')
print('kernel.bin', len(out), 'bytes')
