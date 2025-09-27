local dap = require("dap")

dap.configurations.asm = {
	{
		name = "Launch ./main",
		type = "codelldb",
		request = "launch",
		program = vim.fn.getcwd() .. "/main",
		cwd = vim.fn.getcwd(),
		stopOnEntry = true,
		initCommands = {
			"process connect connect://ubuntu20.orb.local:1234",
		},
	},
	{
		name = "Remote Launch",
		type = "codelldb",
		request = "launch",
		program = vim.fn.getcwd() .. "/main",
		initCommands = {
			"platform select remote-linux", -- For example: 'remote-linux', 'remote-macosx', 'remote-android', etc.
			"platform connect connect://ubuntu20.orb.local:1234",
			"settings set target.inherit-env false", -- See note below.
		},
	},
}
