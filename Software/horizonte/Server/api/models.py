from django.db import models

# Create your models here.
class AstroObject(models.Model):
    nombre = models.CharField(max_length=100)
    constelacion = models.CharField(max_length=100)
    info = models.TextField(default="Sin informacion")
    imageUrl = models.URLField(default="https://upload.wikimedia.org/wikipedia/commons/8/8b/Arcturus_star.jpg")
    ra = models.CharField(max_length=50)
    dec = models.CharField(max_length=50)

    def __str__(self):
        return self.nombre