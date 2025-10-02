local lspconfig = require("lspconfig")
local port = 2057

lspconfig.clangd.setup({
	cmd = {},
	root_dir = lspconfig.util.root_pattern("compile_commands.json", ".git"),
	on_new_config = function(new_config, _)
		-- launch command: socat tcp-listen:2057,reuseaddr,fork exec:'clangd --background-index'
		new_config.cmd = vim.lsp.rpc.connect("127.0.0.1", port)
	end,
})
