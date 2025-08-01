#include "ArtillerySystem.h"
#include "CustomOptions.h"
#include "SystemBox_Extend.h"
#include "PALMemoryProtection.h"

#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>

HOOK_METHOD(ArtillerySystem, Jump, () -> void)
{
    LOG_HOOK("HOOK_METHOD -> ArtillerySystem::Jump -> Begin (ArtillerySystem.cpp)\n")
    super();

    // fixes exploits and potential crash bugs with artillery when jumping while firing
    projectileFactory->ClearAiming();
    projectileFactory->ClearProjectiles();
}

static const Point baseOffset(22, -5);
static Point modifiedOffset = baseOffset;
static bool g_YPosIsFixed = false;

void ParseTargetableArtilleryNode(rapidxml::xml_node<char>* node)
{
    CustomOptionsManager* customOptions = CustomOptionsManager::GetInstance();

    if (node->first_attribute("enabled"))
    {
        auto enabled = node->first_attribute("enabled")->value();
        customOptions->targetableArtillery.defaultValue = EventsParser::ParseBoolean(enabled);
        customOptions->targetableArtillery.currentValue = EventsParser::ParseBoolean(enabled);
    }

    if (node->first_attribute("xOffset")) modifiedOffset.x += boost::lexical_cast<int>(node->first_attribute("xOffset")->value());
    if (node->first_attribute("yOffset")) modifiedOffset.y += boost::lexical_cast<int>(node->first_attribute("yOffset")->value());
    if (node->first_attribute("fixedYPos")) g_YPosIsFixed = EventsParser::ParseBoolean(node->first_attribute("fixedYPos")->value());
}
static bool VTable_Modified = false;
HOOK_METHOD(ArtilleryBox, constructor, (Point pos, ArtillerySystem* sys) -> void)
{
    LOG_HOOK("HOOK_METHOD -> ArtilleryBox::constructor -> Begin (ArtillerySystem.cpp)\n")
    super(pos, sys);

    ArtilleryBox_Extend* extend = static_cast<ArtilleryBox_Extend*>(SB_EX(this));
    extend->artilleryButton.OnInit("systemUI/artilleryButton", location);
    extend->artilleryButton.hitbox.w = 17;
    extend->artilleryButton.hitbox.h = 19;
    extend->offset = modifiedOffset;
    if (!VTable_Modified)
    {
        void** vtable = *reinterpret_cast<void***>(this);
        MEMPROT_SAVE_PROT(dwOldProtect);
        MEMPROT_PAGESIZE();
        int GetHeightModifier_Index = 5;
        MEMPROT_UNPROTECT(&vtable[GetHeightModifier_Index], sizeof(void*), dwOldProtect);
        auto newFunc = &ArtilleryBox::_HS_GetHeightModifier;
        vtable[GetHeightModifier_Index] = reinterpret_cast<void *&>(newFunc);
        MEMPROT_REPROTECT(&vtable[GetHeightModifier_Index], sizeof(void*), dwOldProtect);
        VTable_Modified = true;
    }
}

int ArtilleryBox::_HS_GetHeightModifier()
{
    if (!g_YPosIsFixed && (CustomOptionsManager::GetInstance()->targetableArtillery.currentValue || pSystem->_shipObj.HasAugmentation("ARTILLERY_ORDER")))
    {
        int additionalOffset = baseOffset.y - modifiedOffset.y; //Larger height modifier for smaller y values
        return 21 + additionalOffset;
    }
    else
    {
        return 0;
    }
}

HOOK_METHOD(ArtilleryBox, OnRender, (bool ignoreStatus) -> void)
{
    LOG_HOOK("HOOK_METHOD -> ArtilleryBox::OnRender -> Begin (ArtillerySystem.cpp)\n")
    super(ignoreStatus);
    if (CustomOptionsManager::GetInstance()->targetableArtillery.currentValue || pSystem->_shipObj.HasAugmentation("ARTILLERY_ORDER"))
    {
        ArtilleryBox_Extend* extend = static_cast<ArtilleryBox_Extend*>(SB_EX(this));
        extend->artilleryButton.bActive = artSystem->Functioning();
        ProjectileFactory* armedWeapon = G_->GetCApp()->gui->combatControl.weapControl.armedWeapon;
        extend->artilleryButton.bRenderSelected = armedWeapon == artSystem->projectileFactory;
        extend->offset.y = g_YPosIsFixed ? modifiedOffset.y : modifiedOffset.y - 8 * pSystem->healthState.second;

        CSurface::GL_Translate(extend->offset.x, extend->offset.y);
        extend->artilleryButton.OnRender();
        CSurface::GL_Translate(-extend->offset.x, -extend->offset.y);
        if (extend->artilleryButton.Hovering())
        {
            //TODO: Use GetOverrideTooltip (Not working)
            TextString tooltip = artSystem->projectileFactory->blueprint->desc.tooltip;
            std::string text = tooltip.GetText();
            ShipManager *ship = G_->GetShipManager(pSystem->_shipObj.iShipId);
            auto it = std::find(ship->artillerySystems.begin(), ship->artillerySystems.end(), artSystem);
            if (it != ship->artillerySystems.end())
            {
                int index = std::distance(ship->artillerySystems.begin(), it);
                if (index < 4)
                {
                    std::string hotkey = Settings::GetHotkeyName("aim_artillery" + std::to_string(index + 1));
                    text += "\n" + boost::algorithm::replace_all_copy(G_->GetTextLibrary()->GetText("hotkey"), "\\1", hotkey);
                }
            }
            G_->GetMouseControl()->SetTooltip(text);
        }
    }
}

HOOK_METHOD(SystemBox, MouseMove, (int x, int y) -> void)
{
    LOG_HOOK("HOOK_METHOD -> SystemBox::MouseMove -> Begin (ArtillerySystem.cpp)\n")
    super(x, y);
    if (CustomOptionsManager::GetInstance()->targetableArtillery.currentValue || pSystem->_shipObj.HasAugmentation("ARTILLERY_ORDER"))
    {
        ArtilleryBox_Extend* extend = dynamic_cast<ArtilleryBox_Extend*>(SB_EX(this));
        if (extend) extend->artilleryButton.MouseMove(x - extend->offset.x, y - extend->offset.y, false);
    }
}

void CombatControl::ArmArtillery(ArtillerySystem* artillerySystem)
{
    ProjectileFactory* artilleryWeapon = artillerySystem->projectileFactory;
    artilleryWeapon->ClearAiming();
    weapControl.armedWeapon = artilleryWeapon;
    weapControl.armedSlot = -1;
    UpdateAiming();
}

HOOK_METHOD(CombatControl, KeyDown, (SDLKey key) -> void)
{
    LOG_HOOK("HOOK_METHOD -> CombatControl::KeyDown -> Begin (ArtillerySystem.cpp)\n")
    super(key);
    const auto& artillerySystems = shipManager->artillerySystems;
    bool targetableArtillery = CustomOptionsManager::GetInstance()->targetableArtillery.currentValue || shipManager->HasAugmentation("ARTILLERY_ORDER");
    if (key == Settings::GetHotkey("aim_artillery1") && artillerySystems.size() >= 1 && targetableArtillery)
    {
        ArmArtillery(artillerySystems[0]);
    }
    else if (key == Settings::GetHotkey("aim_artillery2") && artillerySystems.size() >= 2 && targetableArtillery)
    {
        ArmArtillery(artillerySystems[1]);
    }
    else if (key == Settings::GetHotkey("aim_artillery3") && artillerySystems.size() >= 3 && targetableArtillery)
    {
        ArmArtillery(artillerySystems[2]);
    }
    else if (key == Settings::GetHotkey("aim_artillery4") && artillerySystems.size() >= 4 && targetableArtillery)
    {
        ArmArtillery(artillerySystems[3]);
    }
}

HOOK_METHOD(CombatControl, KeyDown, (SDLKey key) -> void)
{
    LOG_HOOK("HOOK_METHOD -> CombatControl::KeyDown -> Begin (ArtillerySystem.cpp)\n")
    super(key);
    const auto& artillerySystems = shipManager->artillerySystems;
    bool targetableArtillery = CustomOptionsManager::GetInstance()->targetableArtillery.currentValue || shipManager->HasAugmentation("ARTILLERY_ORDER");
    if (key == Settings::GetHotkey("artillery1") && artillerySystems.size() >= 1 && targetableArtillery)
    {
        ArmArtillery(artillerySystems[0]);
    }
    else if (key == Settings::GetHotkey("artillery2") && artillerySystems.size() >= 2 && targetableArtillery)
    {
        ArmArtillery(artillerySystems[1]);
    }
    else if (key == Settings::GetHotkey("artillery3") && artillerySystems.size() >= 3 && targetableArtillery)
    {
        ArmArtillery(artillerySystems[2]);
    }
    else if (key == Settings::GetHotkey("artillery4") && artillerySystems.size() >= 4 && targetableArtillery)
    {
        ArmArtillery(artillerySystems[3]);
    }
}

HOOK_METHOD(SystemBox, MouseClick, (bool shift) -> bool)
{
    LOG_HOOK("HOOK_METHOD -> SystemBox::MouseClick -> Begin (ArtillerySystem.cpp)\n")
    bool ret = super(shift);

    bool targetableArtillery = CustomOptionsManager::GetInstance()->targetableArtillery.currentValue || pSystem->_shipObj.HasAugmentation("ARTILLERY_ORDER");
    ArtilleryBox_Extend* extend = dynamic_cast<ArtilleryBox_Extend*>(SB_EX(this));
    if (extend && targetableArtillery && extend->artilleryButton.Hovering())
    {
        ArtillerySystem* artillerySystem = static_cast<ArtillerySystem*>(pSystem);
        G_->GetCApp()->gui->combatControl.ArmArtillery(artillerySystem);
    }
    return ret;
}

void ArtillerySystem::OnLoop_HS_ManualTarget()
{
    ShipSystem::OnLoop();
    int power = GetEffectivePower();
    projectileFactory->powered = power > 0; //TODO: Hook SetPowered
    projectileFactory->SetCooldownModifier(1.5f - 0.25f * power);
    projectileFactory->Update();
    projectileFactory->SetCurrentShip(target);

    /* Begin: inline int GetHackLevel(ShipSystem * this) */
    int iHacked = bUnderAttack ? iHackEffect : 0;
    projectileFactory->SetHacked(iHacked);
    projectileFactory->isArtillery = true;

    // Clear aiming if target is non-hostile enemy
    if (G_->GetShipManager(1) && !G_->GetShipManager(1)->_targetable.hostile && projectileFactory->targetId == 1)
    {
        projectileFactory->ClearAiming();
        projectileFactory->ClearProjectiles();
    }
    /*
    if (projectileFactory->ReadyToFire())
    {
        if (target->IsCloaked()) return;
        //Vanilla code would have automatic targetting and firing here, instead manual firing is handled through WeaponControl


    }
    */
}
HOOK_METHOD_PRIORITY(ArtillerySystem, OnLoop, 9998, () -> void)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> ArtillerySystem::OnLoop -> Begin (ArtillerySystem.cpp)\n")
    if (_shipObj.iShipId == 0 && (CustomOptionsManager::GetInstance()->targetableArtillery.currentValue || _shipObj.HasAugmentation("ARTILLERY_ORDER")))
    {
        OnLoop_HS_ManualTarget();
    }
    else
    {
        super();
    }
}

HOOK_METHOD(WeaponControl, SetAutofiring, (bool on, bool simple) -> void)
{
    LOG_HOOK("HOOK_METHOD -> WeaponControl::SetAutofiring -> Begin (ArtillerySystem.cpp)\n")

    super(on, simple);

    ShipManager* ship = G_->GetShipManager(0);
    if (CustomOptionsManager::GetInstance()->targetableArtillery.currentValue || ship->HasAugmentation("ARTILLERY_ORDER"))
    {
        for (auto arti : ship->artillerySystems)
        {
            arti->projectileFactory->SetAutoFire(autoFiring);
        }
    }
}

// Prevents neutral enemy's artillery from firing when the system is powered by energy crews (i.g. zoltans)
static bool g_haltArtilleryFire = false;
HOOK_METHOD(ArtillerySystem, OnLoop, () -> void)
{
    LOG_HOOK("HOOK_METHOD -> ArtillerySystem::OnLoop -> Begin (ArtillerySystem.cpp)\n")
    if (projectileFactory->targetId == 1 || !G_->GetShipManager(1) || G_->GetShipManager(1)->_targetable.hostile) return super();

    g_haltArtilleryFire = true;
    super();
    g_haltArtilleryFire = false;
}
HOOK_METHOD(ProjectileFactory, ReadyToFire, () -> bool)
{
    LOG_HOOK("HOOK_METHOD -> ProjectileFactory::ReadyToFire -> Begin (ArtillerySystem.cpp)\n")
    if (g_haltArtilleryFire) return false;
    return super();
}
