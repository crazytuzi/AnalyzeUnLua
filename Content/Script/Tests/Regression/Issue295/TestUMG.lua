require "UnLua"

local M = Class()

function M:Construct()
    print("Construct")
end

function M:Destruct()
    print("Destruct")
    self:Release()
end

return M
