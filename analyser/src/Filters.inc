// Included by MainGui.cpp (inside its anonymous namespace). Filters for the Library list and for the Lab: a list of conditions, named saved sets,
// and (for the Lab) the default exclusions and the one-copy-per-song rule. Kept in filters.json in the library folder.

struct FilterCondition { juce::String field, op, value; };

struct FilterSet {
    std::vector<FilterCondition> conditions;
    bool excShort = true, excLong = true, excQuiet = true, excNoGrade = true; //the Lab leaves out: under 60 s, over 10 min, quieter than -25 LUFS, no grade
    bool oneCopy = true;                                                      //the Lab uses one copy of each song
    juce::var toVar() const {
        auto* o = new juce::DynamicObject();
        juce::Array<juce::var> cs;
        for (auto& c : conditions) {auto* x = new juce::DynamicObject(); x->setProperty("field", c.field); x->setProperty("op", c.op); x->setProperty("value", c.value); cs.add(juce::var(x));}
        o->setProperty("conditions", cs);
        o->setProperty("excShort", excShort); o->setProperty("excLong", excLong); o->setProperty("excQuiet", excQuiet); o->setProperty("excNoGrade", excNoGrade); o->setProperty("oneCopy", oneCopy);
        return juce::var(o);
    }
    static FilterSet fromVar(const juce::var& v) {
        FilterSet s;
        if (auto* arr = v.getProperty("conditions", juce::var()).getArray())
            for (auto& c : *arr) s.conditions.push_back({c.getProperty("field", "").toString(), c.getProperty("op", "").toString(), c.getProperty("value", "").toString()});
        auto flag = [&](const char* k, bool def) {return v.hasProperty(k) ? (bool)v.getProperty(k, def) : def;};
        s.excShort = flag("excShort", true); s.excLong = flag("excLong", true); s.excQuiet = flag("excQuiet", true); s.excNoGrade = flag("excNoGrade", true); s.oneCopy = flag("oneCopy", true);
        return s;
    }
};

struct FilterStore {
    FilterSet current;
    bool labUses = false; //the Lab uses the filtered tracks
    std::vector<std::pair<juce::String, FilterSet>> sets;
    void load(const juce::File& folder) {
        *this = FilterStore();
        const juce::var j = juce::JSON::parse(folder.getChildFile("filters.json"));
        if (!j.isObject()) return;
        current = FilterSet::fromVar(j.getProperty("current", juce::var()));
        labUses = (bool)j.getProperty("labUses", false);
        if (auto* arr = j.getProperty("sets", juce::var()).getArray())
            for (auto& s : *arr) sets.push_back({s.getProperty("name", "").toString(), FilterSet::fromVar(s)});
    }
    void save(const juce::File& folder) const {
        auto* o = new juce::DynamicObject();
        o->setProperty("current", current.toVar()); o->setProperty("labUses", labUses);
        juce::Array<juce::var> arr;
        for (auto& s : sets) {juce::var v = s.second.toVar(); v.getDynamicObject()->setProperty("name", s.first); arr.add(v);}
        o->setProperty("sets", arr);
        folder.getChildFile("filters.json").replaceWithText(juce::JSON::toString(juce::var(o)));
    }
};

struct FilterField { juce::String key, label; int type = 0; }; //type: 0 text, 1 number, 2 capital/lowercase

inline juce::StringArray filterOps(int type) {
    if (type == 1) return {"is", "is not", "at least", "at most", "above", "below", "is empty", "is not empty"};
    if (type == 2) return {"is a capital", "is lowercase"};
    return {"contains", "does not contain", "is", "is not", "is empty", "is not empty"};
}

//does one value pass one condition
inline bool conditionPasses(const FilterCondition& c, int type, const juce::String& raw) {
    const juce::String v = raw.trim();
    if (c.op == "is empty") return v.isEmpty();
    if (c.op == "is not empty") return v.isNotEmpty();
    if (type == 2) {
        if (v.isEmpty()) return false;
        const bool cap = juce::CharacterFunctions::isUpperCase(v[0]);
        return c.op == "is a capital" ? cap : !cap;
    }
    if (type == 1) {
        if (v.isEmpty() || !v.containsAnyOf("0123456789")) return false;
        const double x = v.removeCharacters(",").getDoubleValue();
        juce::String t = c.value.trim();
        double y = t.getDoubleValue();
        if (t.contains(":")) y = t.upToFirstOccurrenceOf(":", false, false).getDoubleValue()*60.0 + t.fromFirstOccurrenceOf(":", false, false).getDoubleValue(); //m:ss
        if (c.op == "is") return std::fabs(x - y) < 1e-9;
        if (c.op == "is not") return std::fabs(x - y) >= 1e-9;
        if (c.op == "at least") return x >= y;
        if (c.op == "at most") return x <= y;
        if (c.op == "above") return x > y;
        if (c.op == "below") return x < y;
        return true;
    }
    if (c.op == "contains") return v.containsIgnoreCase(c.value.trim());
    if (c.op == "does not contain") return !v.containsIgnoreCase(c.value.trim());
    if (c.op == "is") return v.equalsIgnoreCase(c.value.trim());
    if (c.op == "is not") return !v.equalsIgnoreCase(c.value.trim());
    return true;
}

class FilterPanel : public juce::Component {
public:
    FilterStore* store = nullptr;
    std::vector<FilterField> fields;
    std::function<juce::StringArray(const juce::String&)> valuesFor;
    std::function<void()> changed;            //the filters changed (the owner applies and saves them)
    std::function<juce::String()> counts;     //"N tracks in the list, M for the Lab"

    FilterPanel() {
        for (auto* l : {&setsLabel, &countLabel, &exclLabel}) {addAndMakeVisible(l); l->setColour(juce::Label::textColourId, kDim);}
        for (auto* b : {&addButton, &clearButton, &saveButton, &deleteButton}) {addAndMakeVisible(b); styleButton(*b);}
        addAndMakeVisible(setsBox);
        setsBox.setColour(juce::ComboBox::backgroundColourId, kPanel); setsBox.setColour(juce::ComboBox::textColourId, kText); setsBox.setColour(juce::ComboBox::outlineColourId, kPanel);
        for (auto* t : {&labUses, &excShort, &excLong, &excQuiet, &excNoGrade, &oneCopy}) {addAndMakeVisible(t); t->setColour(juce::ToggleButton::textColourId, kText); t->setColour(juce::ToggleButton::tickColourId, kAccent);}
        addButton.onClick = [this] {
            if (store->current.conditions.size() >= 8) return;
            store->current.conditions.push_back({fields.empty() ? juce::String() : fields[0].key, filterOps(fields.empty() ? 0 : fields[0].type)[0], {}});
            rebuild(); apply();
        };
        clearButton.onClick = [this] {store->current.conditions.clear(); rebuild(); apply();};
        setsBox.onChange = [this] {
            const int i = setsBox.getSelectedId() - 2;
            if (i >= 0 && i < (int)store->sets.size()) {store->current = store->sets[(size_t)i].second; rebuild(); syncToggles(); apply();}
        };
        saveButton.onClick = [this] {
            auto* w = new juce::AlertWindow("Save this filter", "Name for this set of filters:", juce::MessageBoxIconType::NoIcon, this);
            w->addTextEditor("name", setsBox.getSelectedId() >= 2 ? store->sets[(size_t)setsBox.getSelectedId() - 2].first : juce::String());
            w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) {
                const juce::String name = w->getTextEditorContents("name").trim();
                if (r == 1 && name.isNotEmpty()) {
                    bool found = false;
                    for (auto& s : store->sets) if (s.first.equalsIgnoreCase(name)) {s.second = store->current; found = true;}
                    if (!found) store->sets.push_back({name, store->current});
                    refreshSets(name); if (changed) changed();
                }
                delete w;
            }), false);
        };
        deleteButton.onClick = [this] {
            const int i = setsBox.getSelectedId() - 2;
            if (i >= 0 && i < (int)store->sets.size()) {store->sets.erase(store->sets.begin() + i); refreshSets({}); if (changed) changed();}
        };
        labUses.onClick = [this] {store->labUses = labUses.getToggleState(); apply();};
        excShort.onClick = [this] {store->current.excShort = excShort.getToggleState(); apply();};
        excLong.onClick = [this] {store->current.excLong = excLong.getToggleState(); apply();};
        excQuiet.onClick = [this] {store->current.excQuiet = excQuiet.getToggleState(); apply();};
        excNoGrade.onClick = [this] {store->current.excNoGrade = excNoGrade.getToggleState(); apply();};
        oneCopy.onClick = [this] {store->current.oneCopy = oneCopy.getToggleState(); apply();};
        labUses.setButtonText("The Lab uses the filtered tracks (Set triggers from the library, Re-score library)");
        excShort.setButtonText("leave out tracks shorter than 60 seconds"); excLong.setButtonText("leave out tracks longer than 10 minutes");
        excQuiet.setButtonText("leave out tracks quieter than -25 LUFS"); excNoGrade.setButtonText("leave out tracks with no grade");
        oneCopy.setButtonText("use one copy of each song (the one at the chosen Rate if there is one, else the highest sample rate)");
        exclLabel.setText("When the Lab uses the filtered tracks it also:", juce::dontSendNotification);
        setsLabel.setText("Saved filters", juce::dontSendNotification);
        setSize(760, 600);
    }
    void attach() {rebuild(); refreshSets({}); syncToggles(); updateCounts();}
    void updateCounts() {countLabel.setText(counts ? counts() : juce::String(), juce::dontSendNotification);}

    void paint(juce::Graphics& g) override {
        g.fillAll(kBg);
        g.setColour(kDim); g.setFont(juce::Font(13.0f));
        g.drawFittedText("Only tracks that pass every line stay in the Library list. Lines are 'all of these'. Text matches ignore capitals; for numbers, length can be typed as m:ss.",
                         10, 46, getWidth() - 20, 34, juce::Justification::topLeft, 2);
    }
    void resized() override {
        auto r = getLocalBounds().reduced(10);
        auto top = r.removeFromTop(30);
        setsLabel.setBounds(top.removeFromLeft(100)); setsBox.setBounds(top.removeFromLeft(240)); top.removeFromLeft(6);
        saveButton.setBounds(top.removeFromLeft(90)); top.removeFromLeft(6); deleteButton.setBounds(top.removeFromLeft(80));
        r.removeFromTop(42);
        for (auto* row : rows) {row->setBounds(r.removeFromTop(30)); r.removeFromTop(4);}
        r.removeFromTop((8 - (int)rows.size())*34);
        auto b = r.removeFromTop(30);
        addButton.setBounds(b.removeFromLeft(140)); b.removeFromLeft(6); clearButton.setBounds(b.removeFromLeft(110));
        r.removeFromTop(8);
        labUses.setBounds(r.removeFromTop(26));
        exclLabel.setBounds(r.removeFromTop(24).withTrimmedLeft(24));
        for (auto* t : {&excShort, &excLong, &excQuiet, &excNoGrade, &oneCopy}) t->setBounds(r.removeFromTop(24).withTrimmedLeft(24));
        r.removeFromTop(6);
        countLabel.setBounds(r.removeFromTop(24));
    }

private:
    struct Line : public juce::Component {
        FilterPanel& owner; size_t index;
        juce::ComboBox field, op, value; juce::TextButton remove{"x"};
        Line(FilterPanel& o, size_t i) : owner(o), index(i) {
            for (auto* c : {&field, &op, &value}) {
                addAndMakeVisible(c);
                c->setColour(juce::ComboBox::backgroundColourId, kPanel); c->setColour(juce::ComboBox::textColourId, kText); c->setColour(juce::ComboBox::outlineColourId, kPanel);
            }
            addAndMakeVisible(remove); styleButton(remove);
            value.setEditableText(true);
            auto& c = owner.store->current.conditions[index];
            for (size_t k = 0; k < owner.fields.size(); ++k) field.addItem(owner.fields[k].label, (int)k + 1);
            int ft = 0;
            for (size_t k = 0; k < owner.fields.size(); ++k) if (owner.fields[k].key == c.field) {field.setSelectedId((int)k + 1, juce::dontSendNotification); ft = owner.fields[k].type;}
            fillOps(ft, c.op);
            fillValues(c.field); value.setText(c.value, juce::dontSendNotification);
            value.setVisible(ft != 2 && c.op != "is empty" && c.op != "is not empty");
            field.onChange = [this] {
                const auto& f = owner.fields[(size_t)field.getSelectedId() - 1];
                auto& cc = owner.store->current.conditions[index];
                cc.field = f.key; cc.op = filterOps(f.type)[0]; cc.value = {};
                fillOps(f.type, cc.op); fillValues(f.key); value.setText({}, juce::dontSendNotification);
                value.setVisible(f.type != 2); owner.apply();
            };
            op.onChange = [this] {
                auto& cc = owner.store->current.conditions[index];
                cc.op = op.getText();
                value.setVisible(!(cc.op == "is empty" || cc.op == "is not empty" || cc.op == "is a capital" || cc.op == "is lowercase"));
                owner.apply();
            };
            value.onChange = [this] {owner.store->current.conditions[index].value = value.getText(); owner.apply();};
            remove.onClick = [this] {auto& v = owner.store->current.conditions; const size_t i = index; auto* o = &owner; v.erase(v.begin() + (long)i); juce::MessageManager::callAsync([o] {o->rebuild(); o->apply();});};
        }
        void fillOps(int type, const juce::String& cur) {
            op.clear(juce::dontSendNotification);
            const auto ops = filterOps(type);
            for (int k = 0; k < ops.size(); ++k) {op.addItem(ops[k], k + 1); if (ops[k] == cur) op.setSelectedId(k + 1, juce::dontSendNotification);}
            if (op.getSelectedId() == 0) op.setSelectedId(1, juce::dontSendNotification);
        }
        void fillValues(const juce::String& key) {
            value.clear(juce::dontSendNotification);
            if (!owner.valuesFor) return;
            const auto vals = owner.valuesFor(key);
            for (int k = 0; k < vals.size() && k < 400; ++k) value.addItem(vals[k], k + 1);
        }
        void resized() override {
            auto r = getLocalBounds();
            field.setBounds(r.removeFromLeft(190)); r.removeFromLeft(6);
            op.setBounds(r.removeFromLeft(150)); r.removeFromLeft(6);
            remove.setBounds(r.removeFromRight(32)); r.removeFromRight(6);
            value.setBounds(r);
        }
    };
    juce::OwnedArray<Line> rows;
    juce::Label setsLabel, countLabel, exclLabel;
    juce::ComboBox setsBox;
    juce::TextButton addButton{"Add a condition"}, clearButton{"Clear all"}, saveButton{"Save as..."}, deleteButton{"Delete"};
    juce::ToggleButton labUses, excShort, excLong, excQuiet, excNoGrade, oneCopy;

    void apply() {if (changed) changed(); updateCounts();}
    void rebuild() {
        rows.clear();
        for (size_t i = 0; i < store->current.conditions.size(); ++i) {auto* l = rows.add(new Line(*this, i)); addAndMakeVisible(l);}
        resized(); repaint();
    }
    void syncToggles() {
        labUses.setToggleState(store->labUses, juce::dontSendNotification);
        excShort.setToggleState(store->current.excShort, juce::dontSendNotification); excLong.setToggleState(store->current.excLong, juce::dontSendNotification);
        excQuiet.setToggleState(store->current.excQuiet, juce::dontSendNotification); excNoGrade.setToggleState(store->current.excNoGrade, juce::dontSendNotification);
        oneCopy.setToggleState(store->current.oneCopy, juce::dontSendNotification);
    }
    void refreshSets(const juce::String& select) {
        setsBox.clear(juce::dontSendNotification);
        setsBox.addItem("(choose a saved filter)", 1);
        for (size_t i = 0; i < store->sets.size(); ++i) {setsBox.addItem(store->sets[i].first, (int)i + 2); if (store->sets[i].first == select) setsBox.setSelectedId((int)i + 2, juce::dontSendNotification);}
        if (select.isEmpty()) setsBox.setSelectedId(1, juce::dontSendNotification);
    }
};
