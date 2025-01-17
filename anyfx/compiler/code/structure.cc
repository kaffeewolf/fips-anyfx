//------------------------------------------------------------------------------
//  structure.cc
//  (C) 2013 Gustav Sterbrant
//------------------------------------------------------------------------------
#include "structure.h"
#include "typechecker.h"

namespace AnyFX
{

//------------------------------------------------------------------------------
/**
*/
Structure::Structure()
{
	this->symbolType = Symbol::StructureType;
}

//------------------------------------------------------------------------------
/**
*/
Structure::~Structure()
{
	// empty
}

//------------------------------------------------------------------------------
/**
*/
void
Structure::AddParameter(const Parameter& param)
{
	this->parameters.push_back(param);
}

//------------------------------------------------------------------------------
/**
*/
const std::vector<Parameter>& 
Structure::GetParameters() const
{
	return this->parameters;
}

//------------------------------------------------------------------------------
/**
*/
unsigned 
Structure::CalculateSize() const
{
    unsigned sum = 0;

    unsigned i;
    for (i = 0; i < this->parameters.size(); i++)
    {
        const Parameter& param = this->parameters[i];
        sum += DataType::ToByteSize(param.GetDataType());
    }
    return sum;
}

//------------------------------------------------------------------------------
/**
*/
bool
Structure::IsRecursive(TypeChecker& typeChecker)
{
    unsigned i;
    for (i = 0; i < this->parameters.size(); i++)
    {
        const Parameter& param = this->parameters[i];
        if (param.GetDataType().GetType() == DataType::UserType)
        {
            Symbol* sym = typeChecker.GetSymbol(param.GetDataType().GetName());
            if (this == sym)
            {
                return true;
            }
        }
    }
    return false;
}

//------------------------------------------------------------------------------
/**
*/
std::string
Structure::Format(const Header& header) const
{
	std::string formattedCode;

	formattedCode.append("struct");	
	formattedCode.append(" ");
	formattedCode.append(this->GetName());
	formattedCode.append("\n{\n");
	
	unsigned input, output;
	input = output = 0;
	unsigned i;
	for (i = 0; i < this->parameters.size(); i++)
	{
		const Parameter& param = this->parameters[i];

		// generate parameter with a seemingly invalid shader, since 
		formattedCode.append(param.Format(header, input, output));
	}

	formattedCode.append("};\n\n");
	return formattedCode;
}

//------------------------------------------------------------------------------
/**
*/
void
Structure::Unroll(const std::string& name, std::vector<Variable>& vars, TypeChecker& typechecker)
{
	unsigned i;
	for (i = 0; i < this->parameters.size(); i++)
	{
		const Parameter& param = this->parameters[i];

		// if we have a struct in struct, unroll inner struct (recursively)
		const DataType& type = param.GetDataType();

		// unroll if struct
		if (type.GetType() == DataType::UserType)
		{
			Structure* str = dynamic_cast<Structure*>(typechecker.GetSymbol(type.GetName()));
			str->Unroll(name + "." + param.GetName(), vars, typechecker);
		}
		else
		{
			// create variable from structure parameter
			Variable var;
			var.SetName(name + "." + param.GetName());
			var.SetDataType(type);
			var.SetLine(param.GetLine());
			var.SetFile(param.GetFile());
			var.isArray = param.IsArray();
			var.arraySize = param.GetArraySize();
			vars.push_back(var);
		}
	}
}

//------------------------------------------------------------------------------
/**
*/
void
Structure::TypeCheck(TypeChecker& typechecker)
{
	// attempt to add structure, if this fails, we must stop type checking for this structure
	if (!typechecker.AddSymbol(this)) return;

    // also make sure no two structures are recursively included within eachother
    if (this->IsRecursive(typechecker))
    {
        std::string msg = AnyFX::Format("Recursive inclusion of structures detected, %s\n", this->ErrorSuffix().c_str());
        typechecker.Error(msg);
    }

	unsigned i;
	for (i = 0; i < this->parameters.size(); i++)
	{
		Parameter& param = this->parameters[i];
		param.TypeCheck(typechecker);
	}
}


} // namespace AnyFX
