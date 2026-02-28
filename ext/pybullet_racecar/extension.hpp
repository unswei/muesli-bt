#pragma once

namespace muslisp {

class registrar;

namespace ext::pybullet_racecar {

void register_extension(registrar* r, void* user);

}  // namespace ext::pybullet_racecar
}  // namespace muslisp
