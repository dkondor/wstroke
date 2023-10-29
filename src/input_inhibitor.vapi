
[CCode (cheader_filename = "input_inhibitor.h")]
namespace Inhibitor {
	[CCode (cname = "input_inhibitor_grab")]
	bool grab();
	[CCode (cname = "input_inhibitor_ungrab")]
	void ungrab();
}
