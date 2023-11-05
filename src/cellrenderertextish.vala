/* compile with valac -c cellrenderertextish.vala --pkg gtk+-3.0 --vapidir . --pkg input_inhibitor -C -H cellrenderertextish.h */

public class CellRendererTextish : Gtk.CellRendererText {
	public enum Mode { Text, Key, Popup, Combo }
        public new Mode mode;
	public unowned string[] items;
	
	public Gdk.Pixbuf? icon { get; set; default = null; }

	public signal void key_edited(string path, Gdk.ModifierType mods, uint code);
	public signal void combo_edited(string path, uint row);

	private Gtk.CellEditable? cell;

	public CellRendererTextish() {
		mode = Mode.Text;
		cell = null;
		items = null;
	}

	public CellRendererTextish.with_items(string[] items) {
		mode = Mode.Text;
		cell = null;
		this.items = items;
	}
	
	public void set_items(string[] items_) {
		items = items_;
	}

	public override unowned Gtk.CellEditable? start_editing(Gdk.Event? event, Gtk.Widget widget, string path, Gdk.Rectangle background_area, Gdk.Rectangle cell_area, Gtk.CellRendererState flags) {
		cell = null;
		if (!editable)
			return cell;
		switch (mode) {
			case Mode.Text:
				cell = base.start_editing(event, widget, path, background_area, cell_area, flags);
				break;
			case Mode.Key:
				cell = new CellEditableAccel(this, path, widget);
				break;
			case Mode.Combo:
				cell = new CellEditableCombo(this, path, widget, items);
				break;
			case Mode.Popup:
				cell = new CellEditableDummy();
				break;
		}
		return cell;
	}
	
	public override void render(Cairo.Context ctx, Gtk.Widget widget, Gdk.Rectangle background_area, Gdk.Rectangle cell_area, Gtk.CellRendererState flags) {
		Gdk.cairo_rectangle(ctx, cell_area);
		if(icon != null) {
			Gdk.cairo_set_source_pixbuf(ctx, icon, cell_area.x, cell_area.y + cell_area.height / 2 - icon.height / 2);
			ctx.fill();
			cell_area.x += icon.width + 4;
		}
		base.render(ctx, widget, background_area, cell_area, flags);
	}
	
	public override void get_size(Gtk.Widget widget, Gdk.Rectangle? cell_area, out int x_offset, out int y_offset, out int width, out int height) {
		base.get_size(widget, cell_area, out x_offset, out y_offset, out width, out height);
		if(icon != null) {
			width += icon.width;
			height = int.max(height, icon.height);
		}
	}
	
	public override void get_preferred_height(Gtk.Widget widget, out int minimum_size, out int natural_size) {
		base.get_preferred_height(widget, out minimum_size, out natural_size);
		if(icon != null) {
			minimum_size = int.max(minimum_size, icon.height);
			natural_size = int.max(natural_size, icon.height);
		}
	}
	public override void get_preferred_height_for_width(Gtk.Widget widget, int width, out int minimum_height, out int natural_height) {
		base.get_preferred_height_for_width(widget, width, out minimum_height, out natural_height);
		if(icon != null) {
			minimum_height = int.max(minimum_height, icon.height);
			natural_height = int.max(natural_height, icon.height);
		}
	}
	public override void get_preferred_width(Gtk.Widget widget, out int minimum_size, out int natural_size) {
		base.get_preferred_width(widget, out minimum_size, out natural_size);
		if(icon != null) {
			minimum_size += icon.width;
			natural_size += icon.width;
		}
	}
	public override void get_preferred_width_for_height(Gtk.Widget widget, int height, out int minimum_width, out int natural_width) {
		base.get_preferred_width_for_height(widget, height, out minimum_width, out natural_width);
		if(icon != null) {
			minimum_width += icon.width;
			natural_width += icon.width;
		}
	}
}

class CellEditableDummy : Gtk.EventBox, Gtk.CellEditable {
	public bool editing_canceled { get; set; }
	protected virtual void start_editing(Gdk.Event? event) {
		editing_done();
		remove_widget();
	}
}

class CellEditableAccel : Gtk.EventBox, Gtk.CellEditable {
	public bool editing_canceled { get; set; }
	new CellRendererTextish parent;
	new string path;

	public CellEditableAccel(CellRendererTextish parent, string path, Gtk.Widget widget) {
		this.parent = parent;
		this.path = path;
		editing_done.connect(on_editing_done);
		Gtk.Label label = new Gtk.Label("Key combination...");
		label.set_alignment(0.0f, 0.5f);
		add(label);
		override_background_color(Gtk.StateFlags.NORMAL, widget.get_style_context().get_background_color(Gtk.StateFlags.SELECTED));
		label.override_color(Gtk.StateFlags.NORMAL, widget.get_style_context().get_color(Gtk.StateFlags.SELECTED));
		show_all();
	}

	protected virtual void start_editing(Gdk.Event? event) {
		Gtk.grab_add(this);
		Gdk.keyboard_grab(get_window(), false, event != null ? event.get_time() : Gdk.CURRENT_TIME);
		
		Inhibitor.grab();

/*
		Gdk.DeviceManager dm = get_window().get_display().get_device_manager();
		foreach (Gdk.Device dev in dm.list_devices(Gdk.DeviceType.SLAVE))
			Gtk.device_grab_add(this, dev, true);
*/
		key_press_event.connect(on_key);
	}

	bool on_key(Gdk.EventKey event) {
		if (event.is_modifier != 0)
			return true;
		switch (event.keyval) {
			case Gdk.Key.Super_L:
			case Gdk.Key.Super_R:
			case Gdk.Key.Hyper_L:
			case Gdk.Key.Hyper_R:
				return true;
		}
		Gdk.ModifierType mods = event.state; /* & Gtk.accelerator_get_default_mod_mask(); -- does not work! */

		editing_done();
		remove_widget();

		parent.key_edited(path, mods, event.hardware_keycode);
		return true;
	}
	void on_editing_done() {
		Gtk.grab_remove(this);
		Gdk.keyboard_ungrab(Gdk.CURRENT_TIME);
		Inhibitor.ungrab();

/*
		Gdk.DeviceManager dm = get_window().get_display().get_device_manager();
		foreach (Gdk.Device dev in dm.list_devices(Gdk.DeviceType.SLAVE))
			Gtk.device_grab_remove(this, dev);
*/
	}
}


class CellEditableCombo : Gtk.ComboBoxText, Gtk.CellEditable {
	new CellRendererTextish parent;
	new string path;

	public CellEditableCombo(CellRendererTextish parent, string path, Gtk.Widget widget, string[] items) {
		this.parent = parent;
		this.path = path;
		foreach (string item in items) {
			append_text(item);
		}
		changed.connect(() => parent.combo_edited(path, active));
	}
	
	public virtual void start_editing(Gdk.Event? event) {
		base.start_editing(event);
		show_all();
	}
}
