{
  description = "CrossPoint Reader - Firmware for Xteink X4 e-paper display";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-parts.url = "github:hercules-ci/flake-parts";
  };

  outputs = inputs@{ flake-parts, ... }:
    flake-parts.lib.mkFlake { inherit inputs; } {
      systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];

      perSystem = { config, self', pkgs, lib, system, ... }:
        let
          # Version from platformio.ini
          version = "0.12.0";

          # Create a platformio wrapper with all required dependencies
          platformio = pkgs.platformio-core.overrideAttrs (old: {
            propagatedBuildInputs = (old.propagatedBuildInputs or [ ]) ++ [
              pkgs.python3Packages.packaging
            ];
          });

          # Python with required packages for build scripts
          pythonEnv = pkgs.python3.withPackages (ps: [ ps.pip ]);

          # FHS environment to run PlatformIO with dynamically linked toolchains
          fhsEnv = pkgs.buildFHSEnv {
            name = "pio-env";
            targetPkgs = pkgs: [
              platformio
              pythonEnv
              pkgs.git
              pkgs.zlib
              pkgs.stdenv.cc.cc.lib
            ];
            runScript = "bash";
          };

          # Build script that runs inside FHS environment
          buildScript = pkgs.writeShellScriptBin "build-firmware" ''
            set -e
            echo "Building CrossPoint Reader firmware..."
            echo ""

            # Ensure we're in the project directory
            if [ ! -f platformio.ini ]; then
              echo "Error: platformio.ini not found. Run this from the project root."
              exit 1
            fi

            # Run the build inside FHS environment
            ${fhsEnv}/bin/pio-env -c "pio run -e gh_release"

            echo ""
            echo "Build complete! Firmware at: .pio/build/gh_release/firmware.bin"
          '';

          # Wrapper for pio that runs inside FHS environment
          pioWrapper = pkgs.writeShellScriptBin "pio" ''
            exec ${fhsEnv}/bin/pio-env -c "pio $*"
          '';
        in
        {
          packages = {
            default = buildScript;
            build-script = buildScript;
          };

          devShells.default = pkgs.mkShell {
            packages = [
              # Build tools (wrapped for FHS compatibility)
              fhsEnv
              pioWrapper
              buildScript
              pythonEnv

              # C/C++ language server
              pkgs.clang-tools

              # Python language server
              pkgs.python3Packages.python-lsp-server

              # Nix language server
              pkgs.nixd

              # Git for submodules
              pkgs.git

              # Useful utilities
              pkgs.esptool
            ];

            shellHook = ''
              echo "CrossPoint Reader development environment"
              echo ""
              echo "Build commands:"
              echo "  pio run               - Build firmware (default env, via FHS wrapper)"
              echo "  pio run -e gh_release - Build release firmware"
              echo "  pio run --target upload - Flash to device"
              echo "  build-firmware        - Build release firmware directly"
              echo ""
              echo "Enter FHS shell for direct PlatformIO access:"
              echo "  pio-env               - Enter FHS shell with PlatformIO"
              echo ""
              echo "Language servers available:"
              echo "  clangd   - C/C++ LSP"
              echo "  pylsp    - Python LSP"
              echo "  nixd     - Nix LSP"
            '';
          };

          apps.build = {
            type = "app";
            program = "${buildScript}/bin/build-firmware";
          };
        };
    };
}
