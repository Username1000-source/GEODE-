// Gravity Safe - Geode mod para Geometry Dash (Geode v5 / GD 2.2081)
// Modo Safe, Auto Safe mejorado, gravedad personalizada y panel en la pantalla de pausa.

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/Notification.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>

using namespace geode::prelude;

// ---------------------------------------------------------------------------
//  Utilidades compartidas
// ---------------------------------------------------------------------------
namespace gs {
    constexpr double MIN_G = 0.01;
    constexpr double MAX_G = 5.0;

    inline bool safeManual() { return Mod::get()->getSettingValue<bool>("safe-mode"); }
    inline bool autoSafe() { return Mod::get()->getSettingValue<bool>("auto-safe"); }
    inline bool gravityOn() { return Mod::get()->getSettingValue<bool>("gravity-enabled"); }
    inline double gravity() { return Mod::get()->getSettingValue<double>("gravity"); }

    // La gravedad cuenta como "modificada" si esta activada y no es 1.0
    inline bool gravityModified() { return gravityOn() && std::abs(gravity() - 1.0) > 0.0001; }
    // Auto Safe: se activa solo cuando hay gravedad modificada
    inline bool safeAuto() { return autoSafe() && gravityModified(); }
    inline bool safeActive() { return safeManual() || safeAuto(); }

    // 0.5 -> "0.5", 1.000 -> "1"
    inline std::string fmtG(double g) {
        auto s = fmt::format("{:.3f}", g);
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        return s;
    }

    // Acepta "0.32" o "0,32". Devuelve el valor limitado entre MIN_G y MAX_G.
    inline std::optional<double> parseGravity(std::string s) {
        for (auto& c : s) {
            if (c == ',') c = '.';
        }
        if (s.empty()) return std::nullopt;
        char* end = nullptr;
        double v = std::strtod(s.c_str(), &end);
        if (end == s.c_str() || *end != '\0') return std::nullopt;
        return std::clamp(v, MIN_G, MAX_G);
    }

    // Icono por nombre de frame, con respaldo para que nunca devuelva nullptr
    inline CCSprite* icon(char const* frame) {
        auto cache = CCSpriteFrameCache::sharedSpriteFrameCache();
        if (cache->spriteFrameByName(frame)) return CCSprite::createWithSpriteFrameName(frame);
        if (cache->spriteFrameByName("GJ_infoIcon_001.png")) return CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
        return CCSprite::create();
    }

    inline std::string pauseText() {
        std::string t = "Safe: ";
        if (safeManual()) t += "ON";
        else if (safeAuto()) t += "ON (auto)";
        else t += "OFF";
        t += "  |  Gravedad: ";
        t += gravityModified() ? fmtG(gravity()) : std::string("normal");
        return t;
    }

    inline std::string badgeText() {
        std::string t;
        if (safeActive()) t = safeManual() ? "SAFE MODE" : "SAFE MODE (AUTO)";
        if (gravityModified()) {
            if (!t.empty()) t += "  |  ";
            t += "GRAV " + fmtG(gravity());
            if (!safeActive()) t += "  (SIN SAFE)";
        }
        return t;
    }

    // --- Chip de estado en la pantalla de pausa ---
    inline void updateChip(CCNode* chip) {
        if (!chip) return;
        auto lbl = typeinfo_cast<CCLabelBMFont*>(chip->getChildByID("label"));
        auto bg = typeinfo_cast<CCScale9Sprite*>(chip->getChildByID("bg"));
        auto ic = typeinfo_cast<CCSprite*>(chip->getChildByID("icon"));
        if (!lbl || !bg || !ic) return;

        lbl->setString(pauseText().c_str());
        float w = lbl->getScaledContentSize().width + 36.f;
        bg->setContentSize({w, 22.f});
        bg->setPosition({w / 2.f, 0.f});
        ic->setPosition({13.f, 0.f});
        lbl->setPosition({27.f, 0.f});
        ic->setColor(safeActive() ? ccc3(120, 255, 120) : ccc3(255, 210, 90));
    }

    inline void refreshPauseChip() {
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;
        auto pause = scene->getChildByType<PauseLayer>(0);
        if (!pause) return;
        updateChip(pause->getChildByID("gs-chip"));
    }

    // --- Insignia dentro del nivel ---
    inline void ensureBadge(PlayLayer* pl) {
        if (!pl || !pl->m_uiLayer) return;
        if (pl->m_uiLayer->getChildByID("gs-badge")) return;

        auto win = CCDirector::sharedDirector()->getWinSize();
        auto badge = CCNode::create();
        badge->setID("gs-badge");
        badge->setPosition({22.f, win.height - 52.f});

        auto ic = icon("GJ_lock_001.png");
        ic->setID("icon");
        ic->setScale(0.45f);
        ic->setOpacity(190);

        auto lbl = CCLabelBMFont::create("", "bigFont.fnt");
        lbl->setID("label");
        lbl->setScale(0.3f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({10.f, 0.f});
        lbl->setOpacity(190);

        badge->addChild(ic);
        badge->addChild(lbl);
        pl->m_uiLayer->addChild(badge, 100);
    }

    inline void refreshBadge(PlayLayer* pl = nullptr) {
        if (!pl) pl = PlayLayer::get();
        if (!pl || !pl->m_uiLayer) return;
        ensureBadge(pl);
        auto badge = pl->m_uiLayer->getChildByID("gs-badge");
        if (!badge) return;

        badge->setVisible(safeActive() || gravityModified());
        if (auto lbl = typeinfo_cast<CCLabelBMFont*>(badge->getChildByID("label"))) {
            lbl->setString(badgeText().c_str());
        }
        if (auto ic = typeinfo_cast<CCSprite*>(badge->getChildByID("icon"))) {
            ic->setColor(safeActive() ? ccc3(120, 255, 120) : ccc3(255, 190, 70));
        }
    }
}

// ---------------------------------------------------------------------------
//  Panel (popup) que se abre desde la pantalla de pausa
// ---------------------------------------------------------------------------
class GravitySafePopup : public Popup {
protected:
    CCMenuItemToggler* m_safeToggle = nullptr;
    CCMenuItemToggler* m_autoToggle = nullptr;
    CCMenuItemToggler* m_gravToggle = nullptr;
    TextInput* m_input = nullptr;
    CCLabelBMFont* m_info = nullptr;

    // Coloca un nodo usando coordenadas relativas al centro de la pantalla
    template <class T>
    T* put(T* node, CCNode* parent, float dx, float dy) {
        auto win = CCDirector::sharedDirector()->getWinSize();
        node->setPosition(parent->convertToNodeSpace(ccp(win.width / 2 + dx, win.height / 2 + dy)));
        parent->addChild(node);
        return node;
    }

    bool initPopup() {
        if (!Popup::init(300.f, 250.f)) return false;
        this->setTitle("Gravity Safe");

        auto row = [&](float y) {
            auto bg = CCScale9Sprite::create("square02b_001.png", {0.f, 0.f, 80.f, 80.f});
            bg->setColor({0, 0, 0});
            bg->setOpacity(90);
            bg->setContentSize({266.f, 32.f});
            this->put(bg, m_mainLayer, 0.f, y);
        };
        auto label = [&](char const* text, float y) {
            auto l = CCLabelBMFont::create(text, "bigFont.fnt");
            l->setScale(0.5f);
            l->setAnchorPoint({0.f, 0.5f});
            this->put(l, m_mainLayer, -100.f, y);
        };
        auto iconAt = [&](CCSprite* s, float scale, float y) {
            s->setScale(scale);
            this->put(s, m_mainLayer, -118.f, y);
        };

        float const yS = 64.f;   // Modo Safe
        float const yA = 26.f;   // Auto Safe
        float const yG = -12.f;  // Gravedad on/off
        float const yI = -52.f;  // Textbox + botones
        float const yP = -86.f;  // Presets

        row(yS);
        row(yA);
        row(yG);

        // Iconos + etiquetas
        iconAt(gs::icon("GJ_lock_001.png"), 0.5f, yS);
        label("Modo Safe", yS);

        iconAt(gs::icon("GJ_updateBtn_001.png"), 0.45f, yA);
        label("Auto Safe", yA);

        auto gIcon = gs::icon("GJ_arrow_03_001.png");
        gIcon->setRotation(-90.f);  // flecha hacia abajo = gravedad
        iconAt(gIcon, 0.45f, yG);
        label("Gravedad custom", yG);

        // Toggles
        m_safeToggle = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(GravitySafePopup::onSafe), 0.65f);
        m_safeToggle->toggle(gs::safeManual());
        this->put(m_safeToggle, m_buttonMenu, 112.f, yS);

        m_autoToggle = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(GravitySafePopup::onAuto), 0.65f);
        m_autoToggle->toggle(gs::autoSafe());
        this->put(m_autoToggle, m_buttonMenu, 112.f, yA);

        m_gravToggle = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(GravitySafePopup::onGravToggle), 0.65f);
        m_gravToggle->toggle(gs::gravityOn());
        this->put(m_gravToggle, m_buttonMenu, 112.f, yG);

        // Textbox: escribe cualquier valor (0.32, 0.50, 2.5...)
        m_input = TextInput::create(100.f, "0.50", "bigFont.fnt");
        m_input->setFilter("0123456789.,");
        m_input->setMaxCharCount(6);
        m_input->setString(gs::fmtG(gs::gravity()));
        this->put(m_input, m_mainLayer, -78.f, yI);

        auto applySpr = ButtonSprite::create("Aplicar");
        applySpr->setScale(0.7f);
        auto applyBtn = CCMenuItemSpriteExtra::create(
            applySpr, this, menu_selector(GravitySafePopup::onApply));
        this->put(applyBtn, m_buttonMenu, 28.f, yI);

        auto resetSpr = ButtonSprite::create("Normal");
        resetSpr->setScale(0.7f);
        auto resetBtn = CCMenuItemSpriteExtra::create(
            resetSpr, this, menu_selector(GravitySafePopup::onReset));
        this->put(resetBtn, m_buttonMenu, 100.f, yI);

        // Presets rapidos
        char const* names[4] = {"0.25", "0.50", "0.75", "1.50"};
        int tags[4] = {25, 50, 75, 150};
        for (int i = 0; i < 4; i++) {
            auto spr = ButtonSprite::create(names[i]);
            spr->setScale(0.55f);
            auto btn = CCMenuItemSpriteExtra::create(
                spr, this, menu_selector(GravitySafePopup::onPreset));
            btn->setTag(tags[i]);
            this->put(btn, m_buttonMenu, -96.f + 64.f * i, yP);
        }

        // Estado
        m_info = CCLabelBMFont::create("", "bigFont.fnt");
        m_info->setScale(0.32f);
        this->put(m_info, m_mainLayer, 0.f, -112.f);
        this->refreshInfo();

        return true;
    }

    void refreshInfo() {
        std::string t;
        ccColor3B col = {255, 255, 255};
        if (gs::safeActive()) {
            t = gs::safeManual() ? "SAFE activo" : "SAFE activo (auto)";
            col = {120, 255, 120};
        } else if (gs::gravityModified()) {
            t = "CUIDADO: gravedad modificada sin Safe";
            col = {255, 190, 70};
        } else {
            t = "Todo normal";
        }
        if (gs::gravityModified()) t += "  |  G " + gs::fmtG(gs::gravity());
        m_info->setString(t.c_str());
        m_info->setColor(col);
    }

    void afterChange() {
        this->refreshInfo();
        gs::refreshPauseChip();
        gs::refreshBadge();
        if (gs::safeAuto() && !gs::safeManual()) {
            Notification::create("Auto Safe activado", NotificationIcon::Success, 1.2f)->show();
        }
    }

    void applyGravity(double v) {
        Mod::get()->setSettingValue<double>("gravity", v);
        Mod::get()->setSettingValue<bool>("gravity-enabled", true);
        m_gravToggle->toggle(true);
        m_input->setString(gs::fmtG(v));
        this->afterChange();
    }

    // OJO: dentro del callback de un toggler, isToggled() devuelve el estado ANTERIOR
    void onSafe(CCObject* sender) {
        bool now = !static_cast<CCMenuItemToggler*>(sender)->isToggled();
        Mod::get()->setSettingValue<bool>("safe-mode", now);
        this->afterChange();
    }

    void onAuto(CCObject* sender) {
        bool now = !static_cast<CCMenuItemToggler*>(sender)->isToggled();
        Mod::get()->setSettingValue<bool>("auto-safe", now);
        this->afterChange();
    }

    void onGravToggle(CCObject* sender) {
        bool now = !static_cast<CCMenuItemToggler*>(sender)->isToggled();
        Mod::get()->setSettingValue<bool>("gravity-enabled", now);
        this->afterChange();
    }

    void onApply(CCObject*) {
        auto v = gs::parseGravity(m_input->getString());
        if (!v) {
            Notification::create("Valor invalido (ej: 0.50)", NotificationIcon::Error, 1.5f)->show();
            return;
        }
        this->applyGravity(*v);
    }

    void onReset(CCObject*) {
        Mod::get()->setSettingValue<double>("gravity", 1.0);
        Mod::get()->setSettingValue<bool>("gravity-enabled", false);
        m_gravToggle->toggle(false);
        m_input->setString("1");
        this->afterChange();
    }

    void onPreset(CCObject* sender) {
        this->applyGravity(static_cast<CCNode*>(sender)->getTag() / 100.0);
    }

public:
    static GravitySafePopup* create() {
        auto ret = new GravitySafePopup();
        if (ret->initPopup()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
//  Hook: PlayLayer -> Modo Safe + insignia
// ---------------------------------------------------------------------------
class $modify(GSPlayLayer, PlayLayer) {
    struct Fields {
        bool origTest = false;
    };

    // Safe = "test mode" interno: el juego no guarda % ni victorias
    void syncSafe() {
        m_isTestMode = gs::safeActive() ? true : m_fields->origTest;
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        m_fields->origTest = m_isTestMode;
        this->syncSafe();
        gs::refreshBadge(this);

        if (gs::safeAuto() && !gs::safeManual()) {
            Notification::create(
                "Auto Safe activo (gravedad " + gs::fmtG(gs::gravity()) + ")",
                NotificationIcon::Info, 1.8f)->show();
        }
        return true;
    }

    void resetLevel() {
        this->syncSafe();
        PlayLayer::resetLevel();
        gs::refreshBadge(this);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        this->syncSafe();
        PlayLayer::destroyPlayer(player, object);
    }

    void levelComplete() {
        this->syncSafe();
        PlayLayer::levelComplete();
    }
};

// ---------------------------------------------------------------------------
//  Hook: PlayerObject -> gravedad personalizada
// ---------------------------------------------------------------------------
class $modify(GSPlayerObject, PlayerObject) {
    struct Fields {
        bool applied = false;
    };

    void update(float dt) {
        if (gs::gravityModified() && PlayLayer::get()) {
            m_gravityMod = gs::gravity();
            m_fields->applied = true;
        } else if (m_fields->applied) {
            m_gravityMod = 1.0;
            m_fields->applied = false;
        }
        PlayerObject::update(dt);
    }
};

// ---------------------------------------------------------------------------
//  Hook: PauseLayer -> boton + chip de estado en la pantalla de pausa
// ---------------------------------------------------------------------------
class $modify(GSPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto win = CCDirector::sharedDirector()->getWinSize();

        // Boton redondo con icono
        auto top = gs::icon("GJ_lock_001.png");
        top->setScale(0.7f);
        auto spr = CircleButtonSprite::create(top, CircleBaseColor::Green, CircleBaseSize::Medium);
        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(GSPauseLayer::onGravitySafe));
        btn->setID("gs-open-btn");

        if (auto menu = this->getChildByID("right-button-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        } else {
            auto m = CCMenu::create();
            m->setPosition({win.width - 30.f, 40.f});
            btn->setPosition({0.f, 0.f});
            m->addChild(btn);
            this->addChild(m, 10);
        }

        // Chip de estado arriba a la izquierda
        if (!this->getChildByID("gs-chip")) {
            auto chip = CCNode::create();
            chip->setID("gs-chip");

            auto bg = CCScale9Sprite::create("square02b_001.png", {0.f, 0.f, 80.f, 80.f});
            bg->setID("bg");
            bg->setColor({0, 0, 0});
            bg->setOpacity(110);

            auto ic = gs::icon("GJ_lock_001.png");
            ic->setID("icon");
            ic->setScale(0.4f);

            auto lbl = CCLabelBMFont::create("", "bigFont.fnt");
            lbl->setID("label");
            lbl->setScale(0.35f);
            lbl->setAnchorPoint({0.f, 0.5f});

            chip->addChild(bg);
            chip->addChild(ic);
            chip->addChild(lbl);
            chip->setPosition({12.f, win.height - 18.f});
            this->addChild(chip, 10);
            gs::updateChip(chip);
        }
    }

    void onGravitySafe(CCObject*) {
        if (auto popup = GravitySafePopup::create()) popup->show();
    }
};
