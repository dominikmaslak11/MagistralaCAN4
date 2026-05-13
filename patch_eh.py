content = open('src/core/LearningEngine.h', 'r', encoding='utf-8').read()

old = 'std::unordered_set<uint64_t> detectAutoIncrementBytes() const;'
new = (old + '\n\n'
    + '    // ── Cyclic noise filter ───────────────────────────────\n'
    + '    // Detects bytes where individual bits toggle 0↔1 at high frequency\n'
    + '    std::unordered_set<uint64_t> detectCyclicNoiseBytes() const;\n'
    + '    // key = (id << 8) | byteIdx\n'
    + '\n'
    + '    // ── Noise filter toggle ──────────────────────────────\n'
    + '    void setNoiseFilterEnabled(bool on) { m_noiseFilterEnabled = on; }\n'
    + '    bool noiseFilterEnabled() const { return m_noiseFilterEnabled; }')

content = content.replace(old, new, 1)
open('src/core/LearningEngine.h', 'w', encoding='utf-8').write(content)
print('OK')
