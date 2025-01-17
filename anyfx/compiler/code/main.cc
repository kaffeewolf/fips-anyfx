//------------------------------------------------------------------------------
/**
    AnyFX Compiler entry point
    
    (C) 2013 Gustav Sterbrant
*/
//------------------------------------------------------------------------------
#include "cmdlineargs.h"
#include "util.h"
#include "afxcompiler.h"

//------------------------------------------------------------------------------
/**
*/
int
main(int argc, char** argv)
{
	AnyFX::CmdLineArgs args(argc, argv);
	std::string file;
	std::string output;
	std::string target;
	std::vector<std::string> flags;
	std::vector<std::string> defines;

	AnyFXBeginCompile();
	defines = args.GetArguments("-D");
	std::vector<std::string>& includes = args.GetArguments("-I");
	defines.insert(defines.end(), includes.begin(), includes.end());

	flags = args.GetArguments("/");

	bool validArgs = false;
	if (args.HasArgument("-f"))
	{
		file = args.GetArgument("-f");
		if (args.HasArgument("-o"))
		{
			output = args.GetArgument("-o");
			if (args.HasArgument("-target"))
			{
				target = args.GetArgument("-target");
				AnyFXErrorBlob* errors;
				AnyFXCompile(file, output, nullptr, target, "Intel", defines, flags, &errors);
				if (errors)
				{
					printf(errors->buffer);
					delete errors;
				}
			}
			else
			{
				AnyFX::Emit("Compiler must have target platform\n");
			}
		}
		else
		{
			AnyFX::Emit("Compiler must have output path\n");
		}
	}
	else
	{
		AnyFX::Emit("Compiler must have target file\n");
	}

	AnyFXEndCompile();

	return 0;
}

