## Set Up

### File structures
Ideally, we will have three folder under root /workspaces:
```
gem5/  -> Original Gem5
 ├─ src/
 ├─ ...
CMSpec/
 ├─ CMSpec/  -> Octopus src
 | ├─ header/
 | ├─ MCSim/
 | ├─ src/
 ├─ gem5/  -> Octopus Gem5 Interface
 ├─ configs/ -> Octopus Gem5 sampel config
 ├─ SConscript
 ├─ README.md
ATP-Engine/
 ├─ gem5/
 ├─ SConscript
 ├─ ...
```

If your CMSpec is under `ext/CMSpec`, please do
```shell
mv -r ${root}/gem5/ext/CMSpec ${root}
```

### Build
Building MCSim
```shell
cd ${root}/CMSpec/CMSpec/MCSim/src
make libmcsim.so
```

Building Octopus(CMSpec)
```shell
cd ${root}/CMSpec/CMSpec
mkdir build
cd build
cmake ../ .
make 
```

Build Gem5 + Octopus + ATP:
```shell
cd ${root}
git clone https://github.com/gem5/gem5.git # if you did not have this yet
cd ${root}/gem5
scons EXTRAS=../ATP-Engine:../CMSpec -j $(nproc) build/ARM/gem5.fast
```


## How to run
Sample Arch config: 
1. configs/fs_arm.py
2. configs/unique_cache_hierarchy_complete.py

Sample run command:
```shell
export LD_LIBRARY_PATH=${root}/CMSpec/CMSpec/build:${root}/CMSpec/CMSpec/MCsim/src:$LD_LIBRARY_PATH
${root}/gem5/build/ARM/gem5.fast \
-d ${your_path_to_store_files} \
${root}/gem5/configs/fs_arm.py
```