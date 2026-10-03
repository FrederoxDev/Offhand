import { createFile, UiFile, createMinecraftElement, Panel, PanelProps, Image, GetRef } from "Regolith-Generators"

class Grid extends Panel {
    constructor(props: PanelProps) {
        super(props);
        this.setType("grid");
    }
}

const hud_screen = new UiFile("hud");

const HotbarItemIcon = GetRef("hud", "hotbar_hud_item_icon");
const StackCountLabel = GetRef("common", "stack_count_label");
const ItemDurability = GetRef("hud", "item_durability");
const ItemStorage = GetRef("hud", "item_storage");

// Java's Gui#renderItemHotbar only draws the offhand slot while it holds an item. The icon keeps binding while the
// slot is hidden, so its #item_id_aux (-1 when empty) can show the slot again.
hud_screen.addControl("offhand_slot",
    <Image
        texture="textures/ui/offhand_slot"
        layer={1}
        size={[22, 22]}
        offset={[-110, -1.5]}
        anchors="bottom_middle"
        bindings={[
            {
                binding_type: "view",
                source_control_name: "offhand_item_icon",
                source_property_name: "(#item_id_aux > -1)",
                target_property_name: "#visible"
            }
        ]}
    >
        <Grid
            size={[18, 18]}
            layer={1}
            grid_dimensions={[1, 1]}
            collection_name="offhand_items"
            $hotbar_collection_name="offhand_items"
            $item_collection_name="offhand_items"
            $stack_count_required={true}
            $durability_bar_required={true}
            $storage_bar_required={true}
        >
            <Panel grid_position={[0, 0]} size={[18, 18]}>
                <HotbarItemIcon key="offhand_item_icon" layer={3} size={[16, 16]} $item_renderer_binding_condition="always" />
                <StackCountLabel layer={4} />
                <ItemDurability layer={5} />
                <ItemStorage layer={5} />
            </Panel>
        </Grid>
    </Image>
)

hud_screen.addControl("root_panel",
    <Panel modifications={[
        {
            operation: "insert_front",
            array_name: "controls",
            value: [
                { "offhand_slot@hud.offhand_slot": {} }
            ]
        }
    ]} />
)

createFile(hud_screen.toJson());
