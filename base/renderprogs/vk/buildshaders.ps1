Get-ChildItem -Recurse -Path . | Where-Object {$_.Name -match '.(frag|vert)$'} | ForEach-Object {C:\VulkanSDK\1.0.65.0\Bin\glslangValidator.exe -V $_.FullName -o "C:\Program Files (x86)\Steam\SteamApps\common\DOOM 3 BFG Edition\base\renderprogs\vk\spv\$($_.Name).spv"}