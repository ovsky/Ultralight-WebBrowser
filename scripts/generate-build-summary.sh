#!/bin/bash
# Generate build summary for Linux/macOS builds

set -e

BUILD_DIR="${1:?Build directory required}"
PLATFORM="${2:?Platform required (Linux/macOS)}"
ARCHITECTURE="${3:?Architecture required (x64/arm64)}"
VERSION="${4:-dev}"
COMMIT_SHA="${5:-}"
SDK_VERSION="${6:-Unknown}"
OUTPUT_FILE="${7:-BUILD-SUMMARY.md}"

TIMESTAMP=$(date -u +"%Y-%m-%d %H:%M:%S UTC")

# Initialize summary
cat > "$BUILD_DIR/$OUTPUT_FILE" << EOF
# 🚀 Ultralight WebBrowser Build Summary - $PLATFORM $ARCHITECTURE

**Platform:** $PLATFORM  
**Architecture:** $ARCHITECTURE  
**Version:** $VERSION  
**Build Date:** $TIMESTAMP  
**Commit:** $COMMIT_SHA  
**Ultralight SDK:** $SDK_VERSION

---

## 📦 Generated Packages

EOF

# Find packages
PACKAGES=$(find "$BUILD_DIR" -maxdepth 1 -type f \( -name "*.tar.gz" -o -name "*.deb" -o -name "*.rpm" -o -name "*.dmg" -o -name "*.zip" \) 2>/dev/null || true)

if [ -z "$PACKAGES" ]; then
    echo "⚠️ No packages found in build directory" >> "$BUILD_DIR/$OUTPUT_FILE"
else
    echo "" >> "$BUILD_DIR/$OUTPUT_FILE"
    echo "| Package | Size | SHA256 |" >> "$BUILD_DIR/$OUTPUT_FILE"
    echo "|---------|------|--------|" >> "$BUILD_DIR/$OUTPUT_FILE"
    
    while IFS= read -r pkg; do
        if [ -f "$pkg" ]; then
            SIZE_MB=$(du -m "$pkg" | cut -f1)
            HASH=$(sha256sum "$pkg" | cut -d' ' -f1 | cut -c1-16)
            BASENAME=$(basename "$pkg")
            echo "| \`$BASENAME\` | ${SIZE_MB} MB | \`${HASH}...\` |" >> "$BUILD_DIR/$OUTPUT_FILE"
        fi
    done <<< "$PACKAGES"
fi

cat >> "$BUILD_DIR/$OUTPUT_FILE" << 'EOF'

---

## 🔧 Build Configuration

EOF

# CMake cache info
if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "### CMake Settings" >> "$BUILD_DIR/$OUTPUT_FILE"
    echo "" >> "$BUILD_DIR/$OUTPUT_FILE"
    echo "| Setting | Value |" >> "$BUILD_DIR/$OUTPUT_FILE"
    echo "|---------|-------|" >> "$BUILD_DIR/$OUTPUT_FILE"
    
    # Extract and format CMake settings
    BUILD_TYPE=$(grep "^CMAKE_BUILD_TYPE:" "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d'=' -f2 || echo "N/A")
    COMPILER_ID=$(grep "^CMAKE_CXX_COMPILER_ID:" "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d'=' -f2 || echo "N/A")
    COMPILER_VER=$(grep "^CMAKE_CXX_COMPILER_VERSION:" "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d'=' -f2 || echo "N/A")
    GENERATOR=$(grep "^CMAKE_GENERATOR:" "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d'=' -f2 || echo "N/A")
    BUILD_TESTING=$(grep "^BUILD_TESTING:" "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d'=' -f2 || echo "OFF")
    CREATE_INSTALLER=$(grep "^CREATE_INSTALLER:" "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d'=' -f2 || echo "N/A")
    
    echo "| Build Type | \`$BUILD_TYPE\` |" >> "$BUILD_DIR/$OUTPUT_FILE"
    echo "| Compiler | $COMPILER_ID $COMPILER_VER |" >> "$BUILD_DIR/$OUTPUT_FILE"
    echo "| Generator | $GENERATOR |" >> "$BUILD_DIR/$OUTPUT_FILE"
    echo "| Build Testing | $BUILD_TESTING |" >> "$BUILD_DIR/$OUTPUT_FILE"
    echo "| Create Installer | $CREATE_INSTALLER |" >> "$BUILD_DIR/$OUTPUT_FILE"
    
    echo "" >> "$BUILD_DIR/$OUTPUT_FILE"
else
    echo "### CMake Settings" >> "$BUILD_DIR/$OUTPUT_FILE"
    echo "" >> "$BUILD_DIR/$OUTPUT_FILE"
    echo "ℹ️ CMake cache not available" >> "$BUILD_DIR/$OUTPUT_FILE"
    echo "" >> "$BUILD_DIR/$OUTPUT_FILE"
fi

cat >> "$BUILD_DIR/$OUTPUT_FILE" << 'EOF'

---

## ✅ Verification

EOF

# Check for executable
EXE_PATH=$(find "$BUILD_DIR" -name "Ultralight-WebBrowser" -type f -executable 2>/dev/null | head -n1)
if [ -n "$EXE_PATH" ]; then
    EXE_SIZE_MB=$(du -m "$EXE_PATH" | cut -f1)
    EXE_NAME=$(basename "$EXE_PATH")
    echo "✅ **Executable found:** \`$EXE_NAME\` (${EXE_SIZE_MB} MB)" >> "$BUILD_DIR/$OUTPUT_FILE"
else
    echo "⚠️ **Executable not found** in build directory" >> "$BUILD_DIR/$OUTPUT_FILE"
fi

# Check for assets
if [ -d "$BUILD_DIR/assets" ]; then
    ASSET_COUNT=$(find "$BUILD_DIR/assets" -type f | wc -l)
    echo "✅ **Assets directory found:** $ASSET_COUNT files" >> "$BUILD_DIR/$OUTPUT_FILE"
else
    # Assets might be bundled in a different location
    if [ -d "$BUILD_DIR/Release/assets" ]; then
        ASSET_COUNT=$(find "$BUILD_DIR/Release/assets" -type f | wc -l)
        echo "✅ **Assets directory found:** $ASSET_COUNT files" >> "$BUILD_DIR/$OUTPUT_FILE"
    else
        echo "ℹ️ Assets bundled in package or located elsewhere" >> "$BUILD_DIR/$OUTPUT_FILE"
    fi
fi

# Check for shared libraries (Linux)
if [ "$PLATFORM" = "Linux" ]; then
    REQUIRED_LIBS=("libAppCore.so" "libUltralight.so" "libUltralightCore.so" "libWebCore.so")
    MISSING_LIBS=()
    
    for lib in "${REQUIRED_LIBS[@]}"; do
        if ! find "$BUILD_DIR" -name "$lib" -type f 2>/dev/null | grep -q .; then
            MISSING_LIBS+=("$lib")
        fi
    done
    
    if [ ${#MISSING_LIBS[@]} -eq 0 ]; then
        echo "✅ **All required shared libraries found**" >> "$BUILD_DIR/$OUTPUT_FILE"
    else
        echo "⚠️ **Missing libraries:** ${MISSING_LIBS[*]}" >> "$BUILD_DIR/$OUTPUT_FILE"
    fi
fi

cat >> "$BUILD_DIR/$OUTPUT_FILE" << 'EOF'

---

## 📥 Installation Instructions

EOF

if [ "$PLATFORM" = "Linux" ]; then
    cat >> "$BUILD_DIR/$OUTPUT_FILE" << 'EOF'

### Linux

**Debian/Ubuntu (.deb):**
```bash
sudo dpkg -i Ultralight-WebBrowser-*.deb
sudo apt-get install -f  # Fix dependencies if needed
```

**RedHat/Fedora (.rpm):**
```bash
sudo rpm -i Ultralight-WebBrowser-*.rpm
```

**Portable (.tar.gz):**
```bash
tar xzf Ultralight-WebBrowser-*.tar.gz
cd UltralightWebBrowser
./Ultralight-WebBrowser
```

EOF
elif [ "$PLATFORM" = "macOS" ]; then
    cat >> "$BUILD_DIR/$OUTPUT_FILE" << 'EOF'

### macOS

**DMG Package:**
1. Open the `.dmg` file
2. Drag `Ultralight Web Browser` to Applications
3. Launch from Applications or Spotlight

**Portable (.tar.gz):**
```bash
tar xzf Ultralight-WebBrowser-*.tar.gz
cd UltralightWebBrowser
./Ultralight-WebBrowser
```

EOF
fi

cat >> "$BUILD_DIR/$OUTPUT_FILE" << 'EOF'

---

## 📊 Build Statistics

EOF

# File statistics
TOTAL_FILES=$(find "$BUILD_DIR" -type f | wc -l)
TOTAL_SIZE_MB=$(du -sm "$BUILD_DIR" | cut -f1)

echo "- **Total files:** $TOTAL_FILES" >> "$BUILD_DIR/$OUTPUT_FILE"
echo "- **Total size:** ${TOTAL_SIZE_MB} MB" >> "$BUILD_DIR/$OUTPUT_FILE"

cat >> "$BUILD_DIR/$OUTPUT_FILE" << 'EOF'

---

**Generated by Ultralight WebBrowser CI/CD Pipeline**  
*For more information, visit the [GitHub Repository](https://github.com/yourusername/Ultralight-alt)*

EOF

echo "✅ Build summary generated: $BUILD_DIR/$OUTPUT_FILE"
echo ""
echo "Summary preview:"
cat "$BUILD_DIR/$OUTPUT_FILE"
