// Copyright 2020 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

//css_ref MumbleInstall.dll
//css_ref Wix_bin\SDK\Microsoft.Deployment.WindowsInstaller.dll;

using System;
using System.Linq;
using System.Text.RegularExpressions;
using System.Windows.Forms;
using Microsoft.Deployment.WindowsInstaller;
using WixSharp;
using WixSharp.CommonTasks;

public class ClientInstaller : MumbleInstall {
	public ClientInstaller(string version, string arch) {
		string upgradeGuid = "D269FC55-4F2C-4285-9AA9-4D034AF305C4";
		string[] binaries = null;
		string[] plugins = {
			"amongus.dll",
			"aoc.dll",
			"arma2.dll",
			"bf1.dll",
			"bf2.dll",
			"bf3.dll",
			"bf4.dll",
			"bf4_x86.dll",
			"bf1942.dll",
			"bf2142.dll",
			"bfbc2.dll",
			"bfheroes.dll",
			"blacklight.dll",
			"borderlands.dll",
			"borderlands2.dll",
			"breach.dll",
			"cod2.dll",
			"cod4.dll",
			"cod5.dll",
			"codmw2.dll",
			"codmw2so.dll",
			"cs.dll",
			"dys.dll",
			"etqw.dll",
			"ffxiv.dll",
			"ffxiv_x64.dll",
			"gmod.dll",
			"gtaiv.dll",
			"gtasa.dll",
			"gtav.dll",
			"gw.dll",
			"insurgency.dll",
			"jc2.dll",
			"link.dll",
			"lol.dll",
			"lotro.dll",
			"ql.dll",
			"rl.dll",
			"se.dll",
			"sr.dll",
			"ut3.dll",
			"ut99.dll",
			"ut2004.dll",
			"wolfet.dll",
			"wow.dll",
			"wow_x64.dll"
		};

		string[] licenses = {
			"qt.txt",
			"portaudio.txt",
			"gpl.txt",
			"speex.txt",
			"lgpl.txt",
			"Mumble.rtf"
		};

		if (arch == "x64") {
			// 64 bit
			this.Platform = WixSharp.Platform.x64;
			binaries = new string[] {
				"celt0.0.7.0.dll",
				"opus.dll",
				"rnnoise.dll",
				"speex.dll",
				"mumble.exe",
				"mumble_app.dll",
				"mumble_ol.dll",
				"mumble_ol_helper.exe",
				"mumble_ol_helper_x64.exe",
				"mumble_ol_x64.dll",
				"mumble-g15-helper.exe"
			};
		} else if (arch == "x86") {
			// 32 bit
			this.Platform = WixSharp.Platform.x86;
			binaries = new string[] {
				"celt0.0.7.0.dll",
				"opus.dll",
				"rnnoise.dll",
				"speex.dll",
				"mumble.exe",
				"mumble_app.dll",
				"mumble_ol.dll",
				"mumble_ol_helper.exe",
				"mumble-g15-helper.exe"
			};
		}

		this.Name = "Mumble (client)";
		this.UpgradeCode = Guid.Parse(upgradeGuid);
		this.Version = new Version(version);
		this.OutFileName = "mumble_client-" + this.Version + "-" + arch;
		this.Media.First().Cabinet = "Mumble.cab";

		var progsDir = new Dir(@"%ProgramFiles%");
		var productDir = new Dir("Mumble");
		var installDir = new Dir("client");
		var pluginDir = new Dir("plugins");
		var licenseDir = new Dir("licenses");
		var menuDir = new Dir(@"%ProgramMenu%");
		var desktopDir = new Dir(@"%Desktop%");
		var shortcutDir = new Dir("Mumble");
		var menuShortcut = new ExeFileShortcut("Mumble", "[INSTALLDIR]mumble.exe", arguments: "");
		var deskShortcut = new ExeFileShortcut("Mumble", "[INSTALLDIR]mumble.exe", arguments: "");

		deskShortcut.IconFile = @"..\icons\mumble.ico";
		menuShortcut.IconFile = @"..\icons\mumble.ico";

		shortcutDir.Shortcuts = new ExeFileShortcut[] { menuShortcut };
		desktopDir.Shortcuts = new ExeFileShortcut[] { deskShortcut };

		var binaryFiles = new File[binaries.Length];
		var licenseFiles = new File[licenses.Length];
		var pluginFiles = new File[plugins.Length];

		for (int i = 0; i < binaries.Length; i++) {
			binaryFiles[i] = new File(@"..\..\" + binaries[i]);
		}

		for (int i = 0; i < licenses.Length; i++) {
			licenseFiles[i] = new File(@"..\..\licenses\" + licenses[i]);
		}

		for (int i = 0; i < plugins.Length; i++) {
			pluginFiles[i] = new File(@"..\..\plugins\" + plugins[i]);
		}

		installDir.Files = binaryFiles;
		licenseDir.Files = licenseFiles;
		pluginDir.Files = pluginFiles;

		menuDir.Dirs = new Dir[] { shortcutDir };
		installDir.Dirs = new Dir[] { licenseDir, pluginDir };
		productDir.Dirs = new Dir[] { installDir };
		progsDir.Dirs = new Dir[] { productDir};

		this.Dirs = new Dir[] { 
			progsDir,
			menuDir,
			desktopDir
		};
	}
}

class BuildInstaller 
{
	public static void Main(string[] args) {
		string version = "";
		string arch = "";
		bool isAllLangs = false;

		for (int i = 0; i < args.Length; i++) {
			if (args[i] == "--version" && Regex.IsMatch(args[i + 1], @"^([0-9]+\.){3}[0-9]+$")) {
				version = args[i + 1];
			}

			if (args[i] == "--arch" && (args[i + 1] == "x64" || args[i + 1] == "x86")) {
				arch = args[i + 1];
			}

			if (args[i] == "--all-languages") {
				isAllLangs = true;
			}
		}

		if (version != null && arch != null) {
			var clInstaller = new ClientInstaller(version, arch);
			clInstaller.Version = new Version(version);

			if (isAllLangs) {
				clInstaller.BuildMultilanguageMsi();
			} else {
				clInstaller.BuildMsi();
			}
		} else {
			Console.WriteLine("ERROR - Values for arch or version are null or incorrect!");
			Environment.ExitCode = 0xA0; // Bad argument
		}
	}
}
